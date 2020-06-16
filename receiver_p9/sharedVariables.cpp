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

#include "JFReceiver.h"

size_t frame_buffer_size = 0;
size_t status_buffer_size = 0;
size_t gain_pedestal_data_size = 0;
size_t jf_packet_headers_size = 0;
size_t ib_buffer_size = COMPOSED_IMAGE_SIZE * RDMA_SQ_SIZE * sizeof(int16_t);

receiver_settings_t receiver_settings;
ib_settings_t ib_settings;
experiment_settings_t experiment_settings;

// Last frame with trigger - for consistency measured only for a single module, protected by mutex
uint32_t trigger_frame = 0;
pthread_mutex_t trigger_frame_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  trigger_frame_cond = PTHREAD_COND_INITIALIZER;

// IB buffer usage
int16_t ib_buffer_occupancy[RDMA_SQ_SIZE];
pthread_mutex_t ib_buffer_occupancy_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ib_buffer_occupancy_cond = PTHREAD_COND_INITIALIZER;

// TCP/IP socket
int sockfd;
int accepted_socket; // There is only one accepted socket at the time
pthread_mutex_t accepted_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

// Buffers for communication with the FPGA
int16_t *frame_buffer = NULL;
online_statistics_t *online_statistics = NULL;
header_info_t *jf_packet_headers = NULL;
char *status_buffer = NULL;
uint16_t *gain_pedestal_data = NULL;
char *packet_counter = NULL;
char *ib_buffer = NULL;

pthread_mutex_t cuda_stream_ready_mutex[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
pthread_cond_t  cuda_stream_ready_cond[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
int cuda_stream_ready[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];

pthread_mutex_t writer_threads_done_mutex[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
pthread_cond_t writer_threads_done_cond[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];
int writer_threads_done[NCUDA_STREAMS*CUDA_TO_IB_BUFFER];

std::vector<spot_t> all_spots;
pthread_mutex_t all_spots_mutex = PTHREAD_MUTEX_INITIALIZER;
