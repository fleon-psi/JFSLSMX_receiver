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

#include "../json/single_include/nlohmann/json.hpp"
#include <ctime>
#include <vector>
#include <map>
#include <set>
#include <hdf5.h>

#ifndef OFFLINE
#include <Detector.h>
#endif

#include "../include/JFApp.h"
#define RDMA_RQ_SIZE 16000L // Maximum number of receive elements
#define YPIXEL       (514L * NMODULES * NCARDS / 2)
#define XPIXEL       (2 * 1030L)

#define LZ4_BLOCK_SIZE  0
#define ZSTD_BLOCK_SIZE (8*514*1030)

#define PREVIEW_FREQUENCY 1.0
#define PREVIEW_STRIDE (int(PREVIEW_FREQUENCY/experiment_settings.frame_time))
#define PREVIEW_SIZE (XPIXEL * YPIXEL)

extern pthread_mutex_t spots_statistics;
#define PEDESTAL_TIME_CUTOFF (60*60) // collect pedestal every 1 hour

enum compression_t {JF_COMPRESSION_NONE, JF_COMPRESSION_BSHUF_LZ4, JF_COMPRESSION_BSHUF_ZSTD};
enum write_mode_t  {JF_WRITE_HDF5, JF_WRITE_BINARY, JF_WRITE_ZMQ};

// Settings only necessary for writer
struct writer_settings_t {
	std::string default_path;   // Main location in the file system, where all files are placed
	std::string HDF5_prefix;    // Files are saved in default_path + "/" + HDF5_prefix + "_master.h5" and "_data.h5" 
	int images_per_file;        // Images saved in a single file
	int nthreads;               // Number of threads per card
	compression_t compression;  // Compression
    write_mode_t write_mode;    // Writing mode
    bool timing_trigger;        // Timing mode (true = external triger, false = internal trigger)
    bool hdf18_compat;          // True = (compatibility with HDF5 1.8), False = (use SWMR and VDS)
    std::string tracking_id;    // Dataset tracking ID, assigned by beamline
    std::string influxdb_url;   // URL of InfluxDB database
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

struct gain_pedestal_t {
	uint16_t gainG0[NCARDS*NPIXEL];
	uint16_t gainG1[NCARDS*NPIXEL];
	uint16_t gainG2[NCARDS*NPIXEL];
	uint16_t pedeG1[NCARDS*NPIXEL];
	uint16_t pedeG2[NCARDS*NPIXEL];
	uint16_t pedeG0[NCARDS*NPIXEL];
	uint16_t pixel_mask[NCARDS*NPIXEL];
};

enum parameter_type_t {
    PARAMETER_FLOAT, PARAMETER_BOOL, PARAMETER_UINT, PARAMETER_STRING
};

struct parameter_t {
    std::string units;
    parameter_type_t type;
    double min;
    double max;
    bool read_only;
    void (*output)(nlohmann::json &out);
    void (*input)(nlohmann::json &in);
    std::string description;
    std::set<std::string> allowed_string_values;
};

extern std::map<std::string, parameter_t> detector_options;

struct read_only_exception : public std::exception {

};

struct spot_statistics_t {
    float resolution_limit;
    float wilson_B;
    int resolution_bins;
    std::vector<float> intensity;
    std::vector<float> mean_intensity;
    std::vector<float> log_mean_intensity;
    std::vector<size_t> count;
    std::vector<float> one_over_d2;
    std::vector<float> mean_one_over_d2;
};

void *run_writer_thread(void* thread_arg);
void *run_metadata_thread(void* thread_arg);

extern pthread_t *writer_thread;
extern writer_thread_arg_t *writer_thread_arg;

extern pthread_t metadata_thread[NCARDS];
extern writer_thread_arg_t metadata_thread_arg[NCARDS];

extern gain_pedestal_t gain_pedestal;
extern online_statistics_t online_statistics[NCARDS];

extern experiment_settings_t experiment_settings;
extern writer_connection_settings_t writer_connection_settings[NCARDS];

extern uint8_t writers_done_per_file;
extern pthread_mutex_t writers_done_per_file_mutex;
extern pthread_cond_t writers_done_per_file_cond;

extern size_t total_compressed_size;
extern pthread_mutex_t total_compressed_size_mutex;

extern uint64_t remaining_images[NCARDS];
extern pthread_mutex_t remaining_images_mutex[NCARDS];

extern std::vector<int32_t> preview;
//extern pthread_mutex_t preview_mutex; // not protected by mutex at the moment, but might be used in the future

extern std::vector<spot_t> spots;
extern pthread_mutex_t spots_mutex;

extern std::vector<double> spot_count_per_image;
extern spot_statistics_t spot_statistics;
extern int spot_statistics_sequence; // spot statistics sequence is incremented every time these are updated, so plot can be changed then

extern pthread_mutex_t spots_statistics_mutex;

#ifndef OFFLINE
extern sls::Detector *det;
#endif

extern struct timespec time_pedestalG0;
extern struct timespec time_pedestalG1;
extern struct timespec time_pedestalG2;
extern struct timespec time_start;
extern struct timespec time_end;

extern double mean_pedestalG0[NMODULES*NCARDS];
extern double mean_pedestalG1[NMODULES*NCARDS];
extern double mean_pedestalG2[NMODULES*NCARDS];
extern size_t bad_pixels[NMODULES*NCARDS];

int open_master_hdf5();
int close_master_hdf5();
int open_data_hdf5();
int close_data_hdf5();
int save_data_hdf(char *data, size_t size, size_t frame, int chunk);
int save_binary(char *data, size_t size, int frame_id, int thread_id);

int jfwriter_arm();
int jfwriter_disarm();
int jfwriter_setup();
int jfwriter_close();
int jfwriter_pedestal();
int jfwriter_pedestalG0();
int jfwriter_pedestalG1();
int jfwriter_pedestalG2();

int setup_detector();
int trigger_detector();
int close_detector();

int setup_infiniband(int card_id);
int close_infiniband(int card_id);
int tcp_receive(int sockfd, char *buffer, size_t size);
int connect_to_power9(int card_id);
int disconnect_from_power9(int card_id);
int exchange_magic_number(int sockfd);

void log_error(std::string category, std::string msg);
void log_measurement();
void log_pedestal_G0();
void log_pedestal_G1();
void log_pedestal_G2();
void init_influxdb_client();
void close_influxdb_client();

void update_summation();
void set_default_parameters();

void mean_pedeG0(double out[NMODULES*NCARDS]);
void mean_pedeG1(double out[NMODULES*NCARDS]);
void mean_pedeG2(double out[NMODULES*NCARDS]);
void count_bad_pixel(size_t out[NMODULES*NCARDS]);

// ZeroMQ functions - not used at the moment
int setup_zeromq_context();
int close_zeromq_context();
int setup_zeromq_sockets(void **socket);
int close_zeromq_sockets(void **socket);
int setup_zeromq_pull_socket(void **socket, int number);
int close_zeromq_pull_socket(void **socket);
int send_zeromq(void *zeromq_socket, void *data, size_t data_size, int frame, int chunk);

int update_jpeg_preview(std::vector<uint8_t> &jpeg_out, float contrast = 50.0);
int update_jpeg_preview_log(std::vector<uint8_t> &jpeg_out, float contrast = 50.0);
 
#endif // JFWRITER_H_
