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
        // Read thread ID
	writer_thread_arg_t *arg = (writer_thread_arg_t *)thread_arg;
	int thread_id = arg->thread_id;
	int card_id   = arg->card_id;
        size_t local_compressed_size = 0;

	// Work request
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
        if (writer_settings.compression == JF_COMPRESSION_BSHUF_LZ4)
            compression_buffer = (char *) malloc(bshuf_compress_lz4_bound(COMPOSED_IMAGE_SIZE, experiment_settings.pixel_depth, LZ4_BLOCK_SIZE) + 12);
        if (writer_settings.compression == JF_COMPRESSION_BSHUF_ZSTD)
            compression_buffer = (char *) malloc(bshuf_compress_zstd_bound(COMPOSED_IMAGE_SIZE,experiment_settings.pixel_depth, ZSTD_BLOCK_SIZE) + 12);

        // RDMA buffer size is constant, so only half slots are allocated if pixel is 32-bit (with summation)
        size_t number_of_rqs = RDMA_RQ_SIZE;
        if (experiment_settings.pixel_depth == 4) number_of_rqs = RDMA_RQ_SIZE / 2;

	// Lock is necessary for calculating loop condition - number of remaining frames
	pthread_mutex_lock(&remaining_frames_mutex[card_id]);

	// Receive data and write to file
	while (remaining_frames[card_id] > 0) {
                // At the very end of the data collection, no need of adding new work requests
		bool repost_wr = false;
		if (remaining_frames[card_id] > number_of_rqs) repost_wr = true;
		remaining_frames[card_id]--;
		pthread_mutex_unlock(&remaining_frames_mutex[card_id]);

		// Poll CQ for finished receive requests
		ibv_wc ib_wc;
		int num_comp = ibv_poll_cq(writer_connection_settings[card_id].ib_settings.cq, 1, &ib_wc);
                
                // If no completion finished - wait 100 us
		while (num_comp == 0) {
		        usleep(100);
			num_comp = ibv_poll_cq(writer_connection_settings[card_id].ib_settings.cq, 1, &ib_wc);
		}

                // Error in CQ polling
		if (num_comp < 0) {
			std::cerr << "Failed polling IB Verbs completion queue" << std::endl;
			exit(EXIT_FAILURE);
		}

                // Error in work completion
		if (ib_wc.status != IBV_WC_SUCCESS) {
			std::cerr << "Failed status " << ibv_wc_status_str(ib_wc.status) << " of IB Verbs send request #" << (int)ib_wc.wr_id << " Num comp " << num_comp << std::endl;
			exit(EXIT_FAILURE);
		}

                // Frame ID is saved as immediate value, outside of the buffer
		uint32_t frame_id = ntohl(ib_wc.imm_data);
                // Frame length in bytes
		size_t   frame_size = ib_wc.byte_len;
                // Location in buffer is based on work request ID
		char *ib_buffer_location = writer_connection_settings[card_id].ib_buffer
				+ COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * ib_wc.wr_id;

                // For every i-th frame, save frame content for preview
                // Although there is risk, that preview might be read, while being written, it is less of a problem
                // as compared to potentially slowing down data collection
                // So there is deliberately no mutex in place

                // TODO: Include gaps
                if (frame_id % PREVIEW_STRIDE == 0) {
                    if (experiment_settings.pixel_depth == 4) {
                        for (int i = 0; i < XPIXEL * YPIXEL / NCARDS; i++)
                            preview[i+card_id * (XPIXEL * YPIXEL / NCARDS)] = ((int32_t *) ib_buffer_location)[i];
                    } else {
                        for (int i = 0; i < XPIXEL * YPIXEL / NCARDS; i++)
                            preview[i+card_id * (XPIXEL * YPIXEL / NCARDS)] = ((int16_t *) ib_buffer_location)[i];
                    }
                }

                char *output_buffer;
                size_t output_size;

                switch(writer_settings.compression) {                   
                    case JF_COMPRESSION_NONE:
                        // If there is no compression, data are saved directly from the buffer
                        output_buffer = ib_buffer_location;
                        output_size = frame_size;
                        break;

                    case JF_COMPRESSION_BSHUF_LZ4:
                        // Write bitshuffle header
		        bshuf_write_uint64_BE(compression_buffer, frame_size);
		        bshuf_write_uint32_BE(compression_buffer + 8, LZ4_BLOCK_SIZE);
                        // Compress
                        output_size = bshuf_compress_lz4(ib_buffer_location, compression_buffer + 12, frame_size / experiment_settings.pixel_depth, experiment_settings.pixel_depth, LZ4_BLOCK_SIZE) + 12;
                        output_buffer = compression_buffer;
                        break;

                    case JF_COMPRESSION_BSHUF_ZSTD:
                        // Write bitshuffle header
		        bshuf_write_uint64_BE(compression_buffer, frame_size);
		        bshuf_write_uint32_BE(compression_buffer + 8, ZSTD_BLOCK_SIZE);
                        // Compress
                        output_size = bshuf_compress_zstd(ib_buffer_location, compression_buffer + 12, frame_size / experiment_settings.pixel_depth, experiment_settings.pixel_depth, ZSTD_BLOCK_SIZE) + 12;
                        output_buffer = compression_buffer;
                        break;       
                }

                if (writer_settings.write_hdf5)
       	       	     save_data_hdf(output_buffer, output_size, frame_id, card_id);
       	       	else 
		     save_binary(output_buffer, output_size, frame_id, card_id);

                local_compressed_size += output_size;

		// Post work request again
		if (repost_wr) {
			// Make new work request with the same ID
			// If there is need of new work request
			ib_sg_entry.addr = (uint64_t)(ib_buffer_location);
			ib_wr.wr_id = ib_wc.wr_id;
			ibv_post_recv(writer_connection_settings[card_id].ib_settings.qp, &ib_wr, &ib_bad_recv_wr);
		}
                // Mutex needs locking to calculate loop condition
		pthread_mutex_lock(&remaining_frames_mutex[card_id]);
	}
	pthread_mutex_unlock(&remaining_frames_mutex[card_id]);

        // Calculate total compression size
	pthread_mutex_lock(&total_compressed_size_mutex);
	total_compressed_size += local_compressed_size;;
	pthread_mutex_unlock(&total_compressed_size_mutex);

        // Release compression buffer
        if (compression_buffer != NULL) free(compression_buffer);

	pthread_exit(0);
}
