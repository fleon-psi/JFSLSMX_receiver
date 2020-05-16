/*
 * Copyright 2020 Paul Scherrer Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>

#include <arpa/inet.h> // for ntohl
#include <unistd.h>    // for usleep

#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"

#include "JFWriter.h"

// Taken from bshuf
extern "C" {
    void bshuf_write_uint64_BE(void* buf, uint64_t num);
    void bshuf_write_uint32_BE(void* buf, uint32_t num);
}

void *writer_thread(void* thread_arg) {
	writer_thread_arg_t *arg = (writer_thread_arg_t *)thread_arg;
	int thread_id = arg->thread_id;
	int card_id   = arg->card_id;
        size_t local_compressed_size = 0;

	// Work request
	// Start receiving
	struct ibv_sge ib_sg_entry;
	struct ibv_recv_wr ib_wr, *ib_bad_recv_wr;

	// pointer to packet buffer size and memory key of each packet buffer
	ib_sg_entry.length = COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth;
	ib_sg_entry.lkey   = writer_connection_settings[card_id].ib_buffer_mr->lkey;

	ib_wr.num_sge = 1;
	ib_wr.sg_list = &ib_sg_entry;
	ib_wr.next = NULL;

        // Create buffer to store compression settings
        char *compression_buffer = NULL;
        if (writer_settings.compression == COMPRESSION_BSHUF_LZ4)
            compression_buffer = (char *) malloc(bshuf_compress_lz4_bound(COMPOSED_IMAGE_SIZE, experiment_settings.pixel_depth, LZ4_BLOCK_SIZE) + 12);
        if (writer_settings.compression == COMPRESSION_BSHUF_ZSTD)
            compression_buffer = (char *) malloc(bshuf_compress_zstd_bound(COMPOSED_IMAGE_SIZE,experiment_settings.pixel_depth, ZSTD_BLOCK_SIZE) + 12);

        size_t number_of_rqs = RDMA_RQ_SIZE;
        if (experiment_settings.pixel_depth == 4) number_of_rqs = RDMA_RQ_SIZE / 2;

	// Lock is necessary for calculating loop condition
	pthread_mutex_lock(&remaining_frames_mutex[card_id]);

	// Receive data and write to file
	while (remaining_frames[card_id] > 0) {
		bool repost_wr = false;
		if (remaining_frames[card_id] > number_of_rqs) repost_wr = true;
		remaining_frames[card_id]--;
		pthread_mutex_unlock(&remaining_frames_mutex[card_id]);

		// Poll CQ for finished receive requests
		ibv_wc ib_wc;

		int num_comp = ibv_poll_cq(writer_connection_settings[card_id].ib_settings.cq, 1, &ib_wc);
		while (num_comp == 0) {
		        usleep(100);
			num_comp = ibv_poll_cq(writer_connection_settings[card_id].ib_settings.cq, 1, &ib_wc);
		}

		if (num_comp < 0) {
			std::cerr << "Failed polling IB Verbs completion queue" << std::endl;
			exit(EXIT_FAILURE);
		}

		if (ib_wc.status != IBV_WC_SUCCESS) {
			std::cerr << "Failed status " << ibv_wc_status_str(ib_wc.status) << " of IB Verbs send request #" << (int)ib_wc.wr_id << " Num comp " << num_comp << std::endl;
			exit(EXIT_FAILURE);
		}

		uint32_t frame_id = ntohl(ib_wc.imm_data);
		size_t   frame_size = ib_wc.byte_len;
		char *ib_buffer_location = writer_connection_settings[card_id].ib_buffer
				+ COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * ib_wc.wr_id;

                switch(writer_settings.compression) {
                    case COMPRESSION_NONE:
                        if (writer_settings.write_hdf5 == true)
                            save_data_hdf(ib_buffer_location, frame_size, frame_id, card_id);
                        else 
		            write_frame(ib_buffer_location, frame_size, frame_id, card_id);
                        break;

                    case COMPRESSION_BSHUF_LZ4:
		        bshuf_write_uint64_BE(compression_buffer, COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth);
		        bshuf_write_uint32_BE(compression_buffer + 8, LZ4_BLOCK_SIZE);
                        frame_size = bshuf_compress_lz4(ib_buffer_location, compression_buffer + 12, COMPOSED_IMAGE_SIZE, experiment_settings.pixel_depth, LZ4_BLOCK_SIZE) + 12;
                        if (writer_settings.write_hdf5 == true)
       	       	       	    save_data_hdf(compression_buffer, frame_size, frame_id, card_id);
       	       	       	else 
		            write_frame(compression_buffer, frame_size, frame_id, card_id);
                        break;

                    case COMPRESSION_BSHUF_ZSTD:
		        bshuf_write_uint64_BE(compression_buffer, COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth);
		        bshuf_write_uint32_BE(compression_buffer + 8, ZSTD_BLOCK_SIZE);
                        frame_size = bshuf_compress_zstd(ib_buffer_location, compression_buffer + 12, COMPOSED_IMAGE_SIZE, experiment_settings.pixel_depth, ZSTD_BLOCK_SIZE) + 12;
                        if (writer_settings.write_hdf5 == true)
       	       	       	    save_data_hdf(compression_buffer, frame_size, frame_id, card_id);
       	       	       	else 
		            write_frame(compression_buffer, frame_size, frame_id, card_id);
                        break;       
                }

                local_compressed_size += frame_size;

		// Post new WRs
		if (repost_wr) {
			// Make new work request with the same ID
			// If there is need of new work request
			ib_sg_entry.addr = (uint64_t)(ib_buffer_location);
			ib_wr.wr_id = ib_wc.wr_id;
			ibv_post_recv(writer_connection_settings[card_id].ib_settings.qp, &ib_wr, &ib_bad_recv_wr);
		}
		pthread_mutex_lock(&remaining_frames_mutex[card_id]);
	}
	pthread_mutex_unlock(&remaining_frames_mutex[card_id]);

	pthread_mutex_lock(&total_compressed_size_mutex);
	total_compressed_size += local_compressed_size;;
	pthread_mutex_unlock(&total_compressed_size_mutex);

        if (compression_buffer != NULL) free(compression_buffer);
	pthread_exit(0);
}
