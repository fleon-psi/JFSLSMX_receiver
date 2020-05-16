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

#ifndef JFWRITER_H_
#define JFWRITER_H_

#include <hdf5.h>
#include <Detector.h>

#include "../JFApp.h"

#define RDMA_RQ_SIZE 4096L // Maximum number of receive elements
#define NCARDS       2

#define LZ4_BLOCK_SIZE  0
#define ZSTD_BLOCK_SIZE (8*514*1030)

enum compression_t {COMPRESSION_NONE, COMPRESSION_BSHUF_LZ4, COMPRESSION_BSHUF_ZSTD};

// Settings only necessary for writer
struct writer_settings_t {
	std::string HDF5_prefix;
	int images_per_file;
	int nthreads;
	int nlocations;
	std::string data_location[256];
	std::string main_location;
        compression_t compression; 
        bool write_hdf5;
        size_t elem_size;
        bool timing_trigger;
};
extern writer_settings_t writer_settings;

struct writer_connection_settings_t {
	int sockfd;                 // TCP/IP socket
	std::string receiver_host;  // Receiver host
	uint16_t receiver_tcp_port; // Receiver TCP port
	std::string ib_dev_name;    // IB device name
	ib_settings_t ib_settings;  // IB settings
	char *ib_buffer;            // IB buffer
	ibv_mr *ib_buffer_mr;       // IB buffer memory region for Verbs
};

// Thread information
struct writer_thread_arg_t {
	uint16_t thread_id;
	uint16_t card_id;
};

void *writer_thread(void* threadArg);

struct gain_pedestal_t {
	uint16_t gainG0[NCARDS*NPIXEL];
	uint16_t gainG1[NCARDS*NPIXEL];
	uint16_t gainG2[NCARDS*NPIXEL];
	uint16_t pedeG1[NCARDS*NPIXEL];
	uint16_t pedeG2[NCARDS*NPIXEL];
	uint16_t pedeG0[NCARDS*NPIXEL];
        uint16_t pixel_mask[NCARDS*NPIXEL];
};

extern gain_pedestal_t gain_pedestal;
extern online_statistics_t online_statistics[NCARDS];

extern experiment_settings_t experiment_settings;
extern writer_connection_settings_t writer_connection_settings[NCARDS];

extern uint8_t writers_done_per_file;
extern pthread_mutex_t writers_done_per_file_mutex;
extern pthread_cond_t writers_done_per_file_cond;

extern size_t total_compressed_size;
extern pthread_mutex_t total_compressed_size_mutex;

extern uint64_t remaining_frames[NCARDS];
extern pthread_mutex_t remaining_frames_mutex[NCARDS];

int open_data_hdf5();
int close_data_hdf5();
int save_data_hdf(char *data, size_t size, size_t frame, int chunk);
int save_gain_pedestal_hdf5();
int save_master_hdf5();
int pack_data_hdf5();

int write_frame(char *data, size_t size, int frame_id, int thread_id);

int setup_detector(sls::Detector *det);
int trigger_detector(sls::Detector *det);
int close_detector(sls::Detector *det);

int open_connection_card(int card_id);
int exchange_magic_number(int sockfd);
int close_connection_card(int card_id);

#endif // JFWRITER_H_
