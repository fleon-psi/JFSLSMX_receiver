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

#ifndef _JFRECEIVER_H
#define _JFRECEIVER_H

#include <vector>

#include "../include/JFApp.h"
#define FRAME_LIMIT 1000000L

#define RECEIVING_DELAY 5

#define PTHREAD_ERROR(ret,func) if (ret) printf("%s(%d) %s: err = %d\n",__FILE__,__LINE__, #func, ret), exit(ret)

#define TIMEOUT 600

#define NCUDA_STREAMS 8
#define CUDA_TO_IB_BUFFER 2L // How much larger is IB buffer as compared to CUDA

#define RDMA_SQ_PSN 532
#define RDMA_SQ_SIZE (NCUDA_STREAMS*CUDA_TO_IB_BUFFER*NIMAGES_PER_STREAM) // 3840, size of send queue, must be multiplier of frames per CUDA stream

extern experiment_settings_t experiment_settings;

// Settings only necessary for receiver
struct receiver_settings_t {
	int      card_number;
	uint64_t fpga_mac_addr;
	uint64_t fpga_ip_addr;
	int      compression_threads;
	uint16_t tcp_port;
	std::string gain_file_name[NMODULES];
	std::string pedestal_file_name;
	std::string ib_dev_name;
        int gpu_device;
};
extern receiver_settings_t receiver_settings;

// Definition of strong pixel
struct strong_pixel {
    int16_t col;      // column
    int16_t line;     // line (relative to chunk (2x vertical modules) read by GPU)
    uint32_t photons; // intensity of the pixel
};

// Vector containing all spots in the dataset
extern std::vector<spot_t> all_spots;
extern pthread_mutex_t all_spots_mutex;

// Buffers for communication with the FPGA
extern int16_t *frame_buffer;
extern size_t frame_buffer_size;
extern char *status_buffer;
extern size_t status_buffer_size;
extern uint16_t *gain_pedestal_data;
extern size_t gain_pedestal_data_size;
extern char *packet_counter;
extern size_t jf_packet_headers_size;

// Useful pointers to buffers above
extern online_statistics_t *online_statistics;
extern header_info_t *jf_packet_headers;

// Settings for Infiniband
extern ib_settings_t ib_settings;

// IB buffer
extern size_t ib_buffer_size;
extern char *ib_buffer;

// TCP/IP socket
extern int sockfd;
extern int accepted_socket; // There is only one accepted socket at the time

// Thread information
struct ThreadArg {
	uint16_t ThreadID;
	receiver_settings_t receiver_settings;
};

// Last frame with trigger - for consistency measured only for a single module, protected by mutex
extern uint32_t trigger_frame;
extern pthread_mutex_t trigger_frame_mutex; 
extern pthread_cond_t  trigger_frame_cond;

// IB buffer usage
extern int16_t ib_buffer_occupancy[RDMA_SQ_SIZE];
extern pthread_mutex_t ib_buffer_occupancy_mutex;
extern pthread_cond_t ib_buffer_occupancy_cond;


int setup_snap(uint32_t card_number);
void close_snap();

void *run_snap_thread(void *in_threadarg);
void *run_poll_cq_thread(void *in_threadarg);
void *run_send_thread(void *in_threadarg);
void *run_gpu_thread(void *in_threadarg);

int parse_input(int argc, char **argv);

int setup_gpu(int device); 
int close_gpu();

extern pthread_mutex_t cuda_stream_ready_mutex[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
extern pthread_cond_t  cuda_stream_ready_cond[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
extern int cuda_stream_ready[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];

extern pthread_mutex_t writer_threads_done_mutex[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
extern pthread_cond_t writer_threads_done_cond[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
extern int writer_threads_done[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];

#endif
