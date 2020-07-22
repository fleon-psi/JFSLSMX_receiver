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

#include <unistd.h>
#include <malloc.h>
#include <iostream>
#include <arpa/inet.h>

#include "JFReceiver.h"

uint32_t lastModuleFrameNumber() {
    uint32_t retVal = online_statistics->head[0];
    for (int i = 1; i < NMODULES; i++) {
        if (online_statistics->head[i] < retVal) retVal = online_statistics->head[i];
    }
    return retVal;
}


// Take half of the number, but only if not bad pixel/overload
inline int16_t half16(int16_t in) {
    int16_t tmp = in;
    if ((in > INT16_MIN + 10) && (in > INT16_MAX - 10)) tmp /= 2;
    return tmp;
}

// Take quarter of the number, but only if not bad pixel/overload
inline int16_t quarter16(int16_t in) {
    int16_t tmp = in;
    if ((in > INT16_MIN + 10) && (in < INT16_MAX - 10)) tmp /= 4;
    return tmp;
}

// Copy line and extend multipixels
void copy_line(int16_t *destination, int16_t* source) {
    for (int i = 0; i < 255; i++) destination[i] = source[i];
    for (int i = 1; i < 255; i++) destination[i+258] = source[i+256];
    for (int i = 1; i < 255; i++) destination[i+516] = source[i+512];
    for (int i = 1; i < 256; i++) destination[i+774] = source[i+768];
    for (int i = 0; i < 3; i++) {
        int16_t tmp1 = half16(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        int16_t tmp2 = half16(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
    }
}

// Copy line with multi-pixels (255 and 256)
void copy_line_mid(int16_t *destination, int16_t* source, ssize_t offset) {
    for (int i = 0; i < 255; i++) {
         int16_t tmp = half16(source[i]);
         destination[i] = tmp;
         destination[i+offset] = tmp;
    }
    for (int i = 1; i < 255; i++) {
         int16_t tmp = half16(source[i+256]);
         destination[i+258] = tmp;
         destination[i+258+offset] = tmp;
    }

    for (int i = 1; i < 255; i++) {
         int16_t tmp = half16(source[i+512]);
         destination[i+516] = tmp;
         destination[i+516+offset] = tmp;
    }

    for (int i = 1; i < 256; i++) {
         int16_t tmp = half16(source[i+768]);
         destination[i+774] = tmp;
         destination[i+774+offset] = tmp;
    }

    for (int i = 0; i < 3; i++) {
        int16_t tmp1 = quarter16(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        destination[255+i*258+offset] = tmp1;
        destination[256+i*258+offset] = tmp1;
        int16_t tmp2 = quarter16(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
        destination[257+i*258+offset] = tmp2;
        destination[258+i*258+offset] = tmp2;
    }
}


inline int32_t half32(int32_t in) {
    int32_t tmp = in;
    if ((in > UNDERFLOW_32BIT) && (in < OVERFLOW_32BIT)) tmp /= 2;
    return tmp;
}

inline int32_t quarter32(int32_t in) {
    int32_t tmp = in;
    if ((in > UNDERFLOW_32BIT) && (in < OVERFLOW_32BIT)) tmp /= 4;
    return tmp;
}

void copy_line32(int32_t *destination, int32_t* source) {
    for (int i = 0; i < 255; i++) destination[i] = source[i];
    for (int i = 1; i < 255; i++) destination[i+258] = source[i+256];
    for (int i = 1; i < 255; i++) destination[i+516] = source[i+512];
    for (int i = 1; i < 256; i++) destination[i+774] = source[i+768];
    for (int i = 0; i < 3; i++) {
        int32_t tmp1 = half32(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        int32_t tmp2 = half32(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
    }
}

void copy_line_mid32(int32_t *destination, int32_t* source, size_t offset) {
    for (int i = 0; i < 255; i++) {
         int32_t tmp = half32(source[i]);
         destination[i] = tmp;
         destination[i+offset] = tmp;
    }
    for (int i = 1; i < 255; i++) {
         int32_t tmp = half32(source[i+256]);
         destination[i+258] = tmp;
         destination[i+258+offset] = tmp;
    }

    for (int i = 1; i < 255; i++) {
         int32_t tmp = half32(source[i+512]);
         destination[i+516] = tmp;
         destination[i+516+offset] = tmp;
    }

    for (int i = 1; i < 256; i++) {
         int32_t tmp = half32(source[i+768]);
         destination[i+774] = tmp;
         destination[i+774+offset] = tmp;
    }

    for (int i = 0; i < 3; i++) {
        int32_t tmp1 = quarter32(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        destination[255+i*258+offset] = tmp1;
        destination[256+i*258+offset] = tmp1;
        int32_t tmp2 = quarter32(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
        destination[257+i*258+offset] = tmp2;
        destination[258+i*258+offset] = tmp2;
    }
}

void *run_poll_cq_thread(void *in_threadarg) {
	for (size_t finished_wc = 0; finished_wc < experiment_settings.nimages_to_write; finished_wc++) {
		// Poll CQ to reuse ID
		ibv_wc ib_wc;
		int num_comp  = ibv_poll_cq(ib_settings.cq, 1, &ib_wc);; // number of completions present in the CQ
		while (num_comp == 0) {
			usleep(100);
			num_comp = ibv_poll_cq(ib_settings.cq, 1, &ib_wc);
		}

		if (num_comp < 0) {
			std::cerr << "Failed polling IB Verbs completion queue" << std::endl;
			pthread_exit(0);
		}

		if (ib_wc.status != IBV_WC_SUCCESS) {
			std::cerr << "Failed status " << ibv_wc_status_str(ib_wc.status) << " of IB Verbs send request #" << (int)ib_wc.wr_id << std::endl;
			pthread_exit(0);
		}

		pthread_mutex_lock(&ib_buffer_occupancy_mutex);
		ib_buffer_occupancy[ib_wc.wr_id] = 0;
		pthread_cond_signal(&ib_buffer_occupancy_cond);
		pthread_mutex_unlock(&ib_buffer_occupancy_mutex);
	}
        std::cout << "CQ Poll: Done" << std::endl;
	pthread_exit(0);
}

void mark_chunk_done(size_t chunk) {
     size_t ib_slice = chunk % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);
     pthread_mutex_lock(writer_threads_done_mutex+ib_slice);

     // Increment counter of writers done
     writer_threads_done[ib_slice] ++;
     // If all writers done - wake up GPU thread
     if (writer_threads_done[ib_slice] == receiver_settings.compression_threads)
          pthread_cond_signal(writer_threads_done_cond+ib_slice);

     pthread_mutex_unlock(writer_threads_done_mutex+ib_slice);
}

void wait_for_write_to_chunk(size_t chunk) {
     size_t ib_slice = chunk % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);

     // Make sure that CUDA stream is ready to go
     pthread_mutex_lock(cuda_stream_ready_mutex+ib_slice);
     while (cuda_stream_ready[ib_slice] != chunk)
         pthread_cond_wait(cuda_stream_ready_cond+ib_slice,
                           cuda_stream_ready_mutex+ib_slice);
     pthread_mutex_unlock(cuda_stream_ready_mutex+ib_slice);
}

void *run_send_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    uint32_t current_frame_number = lastModuleFrameNumber();

    size_t images_per_stream = NIMAGES_PER_STREAM * 2 / experiment_settings.pixel_depth;

    // Account for leftover
    size_t total_chunks = experiment_settings.nimages_to_write / images_per_stream;
    if (experiment_settings.nimages_to_write - total_chunks * images_per_stream > 0)
           total_chunks++;

    size_t current_chunk = 0; // assume that receiver_settings.compression_threads << NIMAGES_PER_STREAM

    for (size_t image = arg->ThreadID;
    		image < experiment_settings.nimages_to_write;
    		image += receiver_settings.compression_threads) {
        if (experiment_settings.enable_spot_finding) {
            // Synchronization of GPU part with GPU threads
            size_t new_chunk = image / images_per_stream;

            // If we operate in the same chunk as before, there is no need to synchronize
            if (current_chunk != new_chunk) {

                mark_chunk_done(current_chunk);
                wait_for_write_to_chunk(new_chunk);

                // Update chunk
                current_chunk = new_chunk;
            }
        }

    	// Find free buffer to write
    	int32_t buffer_id;

        // If pixel_depth == 4, then only half of buffer size available
        if  (experiment_settings.pixel_depth == 2) buffer_id = image % (RDMA_SQ_SIZE);
        else  buffer_id = image % (RDMA_SQ_SIZE / 2);

        // Make sure buffer is free
    	pthread_mutex_lock(&ib_buffer_occupancy_mutex);

        while (ib_buffer_occupancy[buffer_id] != 0)
            pthread_cond_wait(&ib_buffer_occupancy_cond, &ib_buffer_occupancy_mutex);

    	pthread_mutex_unlock(&ib_buffer_occupancy_mutex);

        size_t collected_frame = image*experiment_settings.summation;

        if (current_frame_number < (collected_frame+experiment_settings.summation-1) + 2) {
    	// Ensure that all frames were already collected, if not wait to try again
    	    while (current_frame_number < (collected_frame+experiment_settings.summation-1) + 2) {
                float delay_in_frames = (collected_frame+experiment_settings.summation-1) + 2 - current_frame_number;
                if (delay_in_frames < 2) usleep(20);
                else  usleep ((delay_in_frames-1)*experiment_settings.frame_time_detector*1000.0);
                current_frame_number = lastModuleFrameNumber();
            }
        }

        if (image % 100 == 0) {
           std::cout << "Frame :" << image << " Backlog = " << current_frame_number - (collected_frame+experiment_settings.summation-1) << " " << online_statistics->head[0] << " " << online_statistics->head[1] << " " << online_statistics->head[2] << " " << online_statistics->head[3] << " " << online_statistics->good_packets << std::endl;
        }

        if (experiment_settings.conversion_mode == MODE_CONV) {
          // Expand multi-pixels and switch to 2x2 modules settings
          if (experiment_settings.summation == 1) {
            // For summation of 1 buffer is 16-bit, otherwise 32-bit
            int16_t *output_buffer = (int16_t *) (ib_buffer + buffer_id * COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth);
            // Correct geometry to make 2x2 module arrangement
            // Add inter-chip gaps
            // Inter module gaps are not added and should be corrected in processing software
            for (int module = 0; module < NMODULES; module ++) {
                size_t pixel_in  = ((collected_frame % FRAME_BUF_SIZE) * NMODULES + module) * MODULE_LINES * MODULE_COLS;
                size_t line_out = (module / 2) * 514; // Two modules will get the same line
                line_out = 514 * NMODULES / 2 - line_out - 1; // Flip upside down
                size_t pixel_out = (line_out * 2  + (module%2)) * 1030; // 2 modules in one row

                for (uint64_t i = 0; i < MODULE_LINES; i ++) {
                    if ((i == 255) || (i == 256)) {
                       pixel_out -= 2 * 1030;
                       copy_line_mid(output_buffer+pixel_out, frame_buffer + pixel_in, 2*1030);
                       pixel_out -= 2 * 1030;
                    } else {
                       copy_line(output_buffer+pixel_out, frame_buffer + pixel_in);
                       pixel_out -= 2 * 1030;
                    }
                    pixel_in += MODULE_COLS;
               }
            }
          } else {
            // For summation of >= 2 32-bit integers are used
            int32_t *output_buffer = (int32_t *) (ib_buffer + buffer_id * COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth);
            for (int module = 0; module < NMODULES; module ++) {
                size_t line_out = (module / 2) * 514;
                line_out = 514 * NMODULES / 2 - line_out - 1; // Flip upside down
                size_t pixel_out = (line_out * 2 + (module % 2)) * 1030; // 2 modules in one row

                for (uint64_t line = 0; line < MODULE_LINES; line ++) {
                    int32_t summed_buffer[MODULE_COLS];
		    for (int col = 0; col < MODULE_COLS; col++)
                        summed_buffer[col] = 0;

                    for (int j = 0; j < experiment_settings.summation; j++) {
                        size_t pixel0_in = ((((collected_frame + j) % FRAME_BUF_SIZE) * NMODULES +  module) * MODULE_LINES + line ) * MODULE_COLS;
                        for (int col = 0; col < MODULE_COLS; col++) {
                            int16_t tmp = frame_buffer[pixel0_in + col];
                            if (tmp < INT16_MIN + 10) summed_buffer[col] = UNDERFLOW_32BIT;
                            if ((tmp > INT16_MAX - 10) && (summed_buffer[col] > UNDERFLOW_32BIT)) summed_buffer[col] = OVERFLOW_32BIT;
                            if ((summed_buffer[col] > UNDERFLOW_32BIT) && (summed_buffer[col] < OVERFLOW_32BIT)) summed_buffer[col] += tmp;
                        }
                    }
                    if ((line == 255) || (line == 256)) {
                       pixel_out -= 2 * 1030;
                       copy_line_mid32(output_buffer+pixel_out, summed_buffer, 2*1030);
                       pixel_out -= 2 * 1030;
                    } else {
                       copy_line32(output_buffer+pixel_out, summed_buffer);
                       pixel_out -= 2 * 1030;
                    }
                }
            }
          }
        } else
            // For raw data, just copy contest of the buffer
            memcpy(ib_buffer + COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * buffer_id,
                   frame_buffer + (collected_frame % FRAME_BUF_SIZE) * NPIXEL, NPIXEL * sizeof(uint16_t));

    	// Send the frame via RDMA
    	ibv_sge ib_sg;
    	ibv_send_wr ib_wr;
    	ibv_send_wr *ib_bad_wr;

    	memset(&ib_sg, 0, sizeof(ib_sg));
    	ib_sg.addr	 = (uintptr_t)(ib_buffer + COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * buffer_id);
        if (experiment_settings.conversion_mode == MODE_CONV)
                ib_sg.length = COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth;
        else ib_sg.length = NPIXEL * sizeof(uint16_t);

        ib_sg.lkey	 = ib_settings.buffer_mr->lkey;

    	memset(&ib_wr, 0, sizeof(ib_wr));
    	ib_wr.wr_id      = buffer_id;
    	ib_wr.sg_list    = &ib_sg;
    	ib_wr.num_sge    = 1;
    	ib_wr.opcode     = IBV_WR_SEND_WITH_IMM;
    	ib_wr.send_flags = IBV_SEND_SIGNALED;
        ib_wr.imm_data   = htonl(image); // Network order
        int ret;
    	while ((ret = ibv_post_send(ib_settings.qp, &ib_wr, &ib_bad_wr))) {
                if (ret != ENOMEM)
    		   std::cerr << "Sending with IB Verbs failed (ret: " << ret << " buffer: " << buffer_id << " len: " << ib_sg.length << ")" << std::endl;
                // ENONEM error doesn't seem to be problematic
                usleep(10);
    	}
    }

    mark_chunk_done(current_chunk);

    // Case when last chunk has handful of elements and is not covered by this thread
    if (current_chunk != total_chunks - 1)
        mark_chunk_done(total_chunks - 1);

    std::cout << arg->ThreadID << ": Sending done" << std::endl;
    pthread_exit(0);
}
