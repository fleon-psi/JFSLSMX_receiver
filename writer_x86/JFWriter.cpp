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

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <algorithm>
#include <chrono>

#include <endian.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include <libssh/libssh.h>

#include <Detector.h>

#include "JFWriter.h"
#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"

// Move to common header with SNAP code
#define DELAY_FRAMES_STOP_AND_QUIT 5

writer_settings_t writer_settings;
gain_pedestal_t gain_pedestal;
online_statistics_t online_statistics[NCARDS];

experiment_settings_t experiment_settings;
writer_connection_settings_t writer_connection_settings[NCARDS];

uint8_t writers_done_per_file;
pthread_mutex_t writers_done_per_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t writers_done_per_file_cond = PTHREAD_COND_INITIALIZER;

size_t total_compressed_size = 0;
pthread_mutex_t total_compressed_size_mutex = PTHREAD_MUTEX_INITIALIZER;

uint64_t remaining_frames[NCARDS];
pthread_mutex_t remaining_frames_mutex[NCARDS];

int exchange_magic_number(int sockfd);

// Taken from bshuf
extern "C" {
    void bshuf_write_uint64_BE(void* buf, uint64_t num); 
    void bshuf_write_uint32_BE(void* buf, uint32_t num);
}

int TCP_connect(int &sockfd, std::string hostname, uint16_t port) {
	// Use socket to exchange connection information
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == 0) {
		std::cout << "Socket error" << std::endl;
		return 1;
	}

	addrinfo *host_data;

	char txt_buffer[6];
	snprintf(txt_buffer, 6, "%d", port);

	if (getaddrinfo(hostname.c_str(), txt_buffer,  NULL, &host_data)) {
		std::cout << "Host not found" << std::endl;
		return 1;
	}
	if (host_data == NULL) {
		std::cout << "Host " << hostname << " not found" << std::endl;
		return 1;
	}
	if (connect(sockfd, host_data[0].ai_addr, host_data[0].ai_addrlen) < 0) {
		std::cout << "Cannot connect to server" << std::endl;
		return 1;
	}

	return exchange_magic_number(sockfd);
}

void TCP_exchange_IB_parameters(int sockfd, ib_settings_t &ib_settings, ib_comm_settings_t *remote) {
	ib_comm_settings_t local;
	local.qp_num = ib_settings.qp->qp_num;
	local.dlid = ib_settings.port_attr.lid;

	// Receive parameters
	read(sockfd, remote, sizeof(ib_comm_settings_t));

	// Send parameters
	send(sockfd, &local, sizeof(ib_comm_settings_t), 0);
}

int parse_input(int argc, char **argv) {
	int opt;
	experiment_settings.energy_in_keV      = 12.4;
	experiment_settings.pedestalG0_frames  = 0;
	experiment_settings.conversion_mode    = MODE_CONV;
	experiment_settings.nframes_to_collect = 16384;
	experiment_settings.nframes_to_write   = 0;
        experiment_settings.summation          = 1;
        experiment_settings.detector_distance  = 80.0;
        experiment_settings.beam_x             = 514.0+18.0;
        experiment_settings.beam_y             = (1030.0+36.0)*(NMODULES*NCARDS/4.0);
        experiment_settings.frame_time         = 0.000500;
        experiment_settings.count_time         = 0.000470;
        experiment_settings.first_frame_number = 1;
        experiment_settings.ntrigger           = 0;
        
        writer_settings.main_location   = "/mnt/n1ssd";
	writer_settings.HDF5_prefix     = "";
	writer_settings.images_per_file = 1000;
        writer_settings.compression     = COMPRESSION_NONE;
        writer_settings.write_hdf5      = false;

        writer_settings.nthreads = NCARDS;
        writer_settings.nlocations = 0;
        writer_settings.timing_trigger = true;

	//These parameters are not changeable at the moment
	writer_connection_settings[0].ib_dev_name = "mlx5_1";
	writer_connection_settings[0].receiver_host = "mx-ic922-1";
	writer_connection_settings[0].receiver_tcp_port = 52320;

	if (NCARDS == 2) {
		writer_connection_settings[1].ib_dev_name = "mlx5_12";
		writer_connection_settings[1].receiver_host = "mx-ic922-1";
		writer_connection_settings[1].receiver_tcp_port = 52321;
	}

	while ((opt = getopt(argc,argv,":E:P:012LZRc:w:f:i:hqt:rsS:RX:Y:D:F:C:T:AN:")) != EOF)
		switch(opt)
		{
                case 'A':
                        writer_settings.timing_trigger = false;
                        break;
                case 'T':
                        experiment_settings.transmission = atof(optarg);
                        break;
                case 'X':
                        experiment_settings.beam_x = atof(optarg);
                        break;
                case 'Y':
                        experiment_settings.beam_y = atof(optarg);
                        break;
                case 'D':
                        experiment_settings.detector_distance = atof(optarg);
                        break;
                case 'F':
                        experiment_settings.frame_time = atof(optarg);
                        break;
                case 'C':
                        experiment_settings.count_time = atof(optarg);
                        break;
		case 'E':
			experiment_settings.energy_in_keV = atof(optarg);
			break;
		case 'P':
			experiment_settings.pedestalG0_frames  = atoi(optarg);
			break;
		case '0':
			experiment_settings.conversion_mode = MODE_PEDEG0;
			break;
		case '1':
			experiment_settings.conversion_mode = MODE_PEDEG1;
			break;
		case '2':
			experiment_settings.conversion_mode = MODE_PEDEG2;
			break;
		case 'R':
			experiment_settings.conversion_mode = MODE_RAW;
			break;
		case 'L':
			writer_settings.compression = COMPRESSION_BSHUF_LZ4;
			break;
		case 'Z':
			writer_settings.compression = COMPRESSION_BSHUF_ZSTD;
			break;
                case 'h':
                        writer_settings.write_hdf5 = true;
                        break;
		case 'q':
			experiment_settings.conversion_mode = 255;
			break;
		case 'c':
			experiment_settings.nframes_to_collect = atoi(optarg);
			break;
		case 'w':
			experiment_settings.nframes_to_write   = atoi(optarg);
			break;
		case 'f':
			writer_settings.HDF5_prefix = std::string(optarg);
			break;
		case 'i':
			writer_settings.images_per_file = atoi(optarg);
			break;
		case 'S':
			experiment_settings.summation = atoi(optarg);
			break;
		case 't':
			writer_settings.nthreads = atoi(optarg) * NCARDS;
			break;
		case 'r':
			writer_settings.nlocations = 4;
			writer_settings.data_location[0] = "/mnt/n0ram";
			writer_settings.data_location[1] = "/mnt/n1ram";
			writer_settings.data_location[2] = "/mnt/n2ram";
			writer_settings.data_location[3] = "/mnt/n3ram";
			break;
		case 's':
			writer_settings.nlocations = 4;
			writer_settings.data_location[0] = "/mnt/n0ssd";
			writer_settings.data_location[1] = "/mnt/n1ssd";
			writer_settings.data_location[2] = "/mnt/n2ssd";
			writer_settings.data_location[3] = "/mnt/n3ssd";
			break;
		case ':':
			break;
		case '?':
			break;
		}

        if (experiment_settings.conversion_mode == MODE_RAW) {
            writer_settings.compression = COMPRESSION_NONE; // Raw data are not well compressible
            experiment_settings.summation = 1; // No summation possible
        }

        if ((experiment_settings.conversion_mode == MODE_PEDEG0) || 
            (experiment_settings.conversion_mode == MODE_PEDEG1) ||
            (experiment_settings.conversion_mode == MODE_PEDEG2)) {
            experiment_settings.nframes_to_write = 0; // Don't write frames, when collecting pedestal
            writer_settings.compression = COMPRESSION_NONE; // No compression allowed
            experiment_settings.summation = 1; // No summation possible
        }

        if (experiment_settings.summation == 1) experiment_settings.pixel_depth = 2;
        else experiment_settings.pixel_depth = 4;

	return 0;
}

// Save image data "as is" binary
int write_frame(char *data, size_t size, int frame_id, int thread_id) {
	char buff[12];
	snprintf(buff,12,"%08d_%01d", frame_id, thread_id);
	std::string prefix = "";
	if (writer_settings.nlocations > 0)
		prefix = writer_settings.data_location[frame_id % writer_settings.nlocations] + "/";
	std::string filename = prefix + writer_settings.HDF5_prefix+"_"+std::string(buff) + ".img";
	std::ofstream out_file(filename.c_str(), std::ios::binary | std::ios::out);
	if (!out_file.is_open()) return 1;
	out_file.write(data,size);
	out_file.close();
	return 0;
}

int allocate_memory(int card_id) {
	// IB buffer
	writer_connection_settings[card_id].ib_buffer = (char *) malloc(RDMA_RQ_SIZE * COMPOSED_IMAGE_SIZE * sizeof(uint16_t));
        if (writer_connection_settings[card_id].ib_buffer == NULL) return 1;
        return 0;
}

int open_connection_card(int card_id) {
	if (TCP_connect(writer_connection_settings[card_id].sockfd,
			writer_connection_settings[card_id].receiver_host,
			writer_connection_settings[card_id].receiver_tcp_port) == 1)
		return 1;

	// Provide experiment settings to receiver
	send(writer_connection_settings[card_id].sockfd,
			&experiment_settings, sizeof(experiment_settings_t), 0);

	if (experiment_settings.conversion_mode == 255) {
		return 0;
	}

	// Setup Infiniband connection
	setup_ibverbs(writer_connection_settings[card_id].ib_settings,
			writer_connection_settings[card_id].ib_dev_name, 1, RDMA_RQ_SIZE+1);

	// Exchange information with remote host
	ib_comm_settings_t remote;
	TCP_exchange_IB_parameters(writer_connection_settings[card_id].sockfd,
			writer_connection_settings[card_id].ib_settings, &remote);

	if (writer_connection_settings[card_id].ib_buffer == NULL) {
		std::cerr << "Memory allocation error" << std::endl;
		return 1;
	}

	writer_connection_settings[card_id].ib_buffer_mr =
			ibv_reg_mr(writer_connection_settings[card_id].ib_settings.pd,
					writer_connection_settings[card_id].ib_buffer, 
                                   RDMA_RQ_SIZE * COMPOSED_IMAGE_SIZE * sizeof(uint16_t), IBV_ACCESS_LOCAL_WRITE);
	if (writer_connection_settings[card_id].ib_buffer_mr == NULL) {
		std::cerr << "Failed to register IB memory region." << std::endl;
		return 1;
	}

	// Post WRs
	// Start receiving
  
        size_t number_of_rqs = RDMA_RQ_SIZE;
        if (experiment_settings.pixel_depth == 4) number_of_rqs = RDMA_RQ_SIZE / 2;
        size_t entry_size    = COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth;

	struct ibv_sge ib_sg_entry;
	struct ibv_recv_wr ib_wr, *ib_bad_recv_wr;

	// pointer to packet buffer size and memory key of each packet buffer
	ib_sg_entry.length = entry_size;
	ib_sg_entry.lkey = writer_connection_settings[card_id].ib_buffer_mr->lkey;

	ib_wr.num_sge = 1;
	ib_wr.sg_list = &ib_sg_entry;
	ib_wr.next = NULL;

	for (size_t i = 0; (i < number_of_rqs) && (i < experiment_settings.nframes_to_write); i++)
	{
		ib_sg_entry.addr = (uint64_t)(writer_connection_settings[card_id].ib_buffer + COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth*i);
		ib_wr.wr_id = i;
		ibv_post_recv(writer_connection_settings[card_id].ib_settings.qp,
				&ib_wr, &ib_bad_recv_wr);
	}

	// Switch to ready to receive
	switch_to_rtr(writer_connection_settings[card_id].ib_settings,
			remote.rq_psn, remote.dlid, remote.qp_num);
	std::cout << "IB Ready to receive" << std::endl;

	return 0;
}

int tcp_receive(int sockfd, char *buffer, size_t size) {
    size_t remaining_size = size;
    while (remaining_size > 0) {
	ssize_t received = read(sockfd, buffer + (size - remaining_size), remaining_size);
        if (received <= 0) {
            std::cerr << "Error reading from TCP/IP socket" << std::endl;
            return 1;
        }
        else remaining_size -= received;
    }
    return 0;
}

int exchange_magic_number(int sockfd) {
	uint64_t magic_number;

	// Receive magic number
	read(sockfd, &magic_number, sizeof(uint64_t));
	// Reply with whatever was received
	send(sockfd, &magic_number, sizeof(uint64_t), 0);
	if (magic_number != TCPIP_CONN_MAGIC_NUMBER) {
		std::cerr << "Mismatch in TCP/IP communication" << std::endl;                
		return 1;
	}
	std::cout << "Magic number OK" << std::endl;
	return 0;
}

int trigger_omega() {
	ssh_session my_ssh_session;	
	my_ssh_session = ssh_new();
        if (my_ssh_session == NULL) {
             std::cerr << "Failure creating SSH session" << std::endl;
             return 1;
        }
        int port = 22;
        ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, "10.10.10.9");
        ssh_options_set(my_ssh_session, SSH_OPTIONS_PORT, &port);
        ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, "pi");

        int rc = ssh_connect(my_ssh_session);
        if (rc != SSH_OK) {
             std::cerr << "Failure in SSH connection to Omega Onion2" << std::endl;
             return 1;
        }

        //enum ssh_known_hosts_e state;
        //state = ssh_session_is_known_server(session);
        //if (state != SSH_KNOWN_HOSTS_OK) {
        //     std::cerr << "Failure in SSH connection to Omega Onion2 (key unknown)" << std::endl;
        //     ssh_disconnect(my_ssh_session);
       	//     ssh_free(my_ssh_session);
        //     return 1;
        //}

        rc = ssh_userauth_password(my_ssh_session, NULL, "filipspi");
        if (rc != SSH_AUTH_SUCCESS) {
             std::cerr << "Failure in SSH connection to Omega Onion2 (auth error)" << std::endl;
             ssh_disconnect(my_ssh_session);
       	     ssh_free(my_ssh_session);
             return 1;
       	}

        ssh_channel my_ssh_channel = ssh_channel_new(my_ssh_session);
        if (my_ssh_channel == NULL) { 
             std::cerr << "Failure in SSH connection to Omega Onion2 (cannot open channel)" << std::endl;
             ssh_disconnect(my_ssh_session);
             ssh_free(my_ssh_session);
             return 1;
        }


        rc = ssh_channel_open_session(my_ssh_channel);
        if (rc != SSH_OK) {
             std::cerr << "Failure in SSH connection to Omega Onion2 (cannot open session)" << std::endl;
             ssh_disconnect(my_ssh_session);
             ssh_free(my_ssh_session);
             return 1;
        }

 
        rc = ssh_channel_request_exec(my_ssh_channel, "bash run");
        if (rc != SSH_OK) {
             std::cerr << "Failure in SSH connection to Omega Onion2 (cannot exec command)" << std::endl;
             ssh_disconnect(my_ssh_session);
             ssh_free(my_ssh_session);
             return 1;
        }
        ssh_channel_send_eof(my_ssh_channel);
        ssh_channel_close(my_ssh_channel);
        ssh_channel_free(my_ssh_channel);
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        std::cout << "SSH OK" << std::endl;
        return 0;
}

int close_connection_card(int card_id) {
        if (experiment_settings.conversion_mode == 255) {
                close(writer_connection_settings[card_id].sockfd);
                return 0;
        }
        
	// Send pedestal, header data and collection statistics
	read(writer_connection_settings[card_id].sockfd,
			&(online_statistics[card_id]), sizeof(online_statistics_t));

        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.gainG0 + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));
        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.gainG1 + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));
        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.gainG2 + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));
        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.pedeG1 + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));
        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.pedeG2 + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));
        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.pedeG0 + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));
        tcp_receive(writer_connection_settings[card_id].sockfd,
                    (char *) (gain_pedestal.pixel_mask + card_id * NPIXEL), NPIXEL * sizeof(uint16_t));

	// Check magic number again - but don't quit, as the program is finishing anyway soon
	exchange_magic_number(writer_connection_settings[card_id].sockfd);

	// Close IB connection
	ibv_dereg_mr(writer_connection_settings[card_id].ib_buffer_mr);
	close_ibverbs(writer_connection_settings[card_id].ib_settings);

	// Free memory buffer
	free(writer_connection_settings[card_id].ib_buffer);

	// Close TCP/IP socket
	close(writer_connection_settings[card_id].sockfd);

	return 0;
}

void *setup_thread(void* thread_arg) {
	writer_thread_arg_t *arg = (writer_thread_arg_t *)thread_arg;
	int card_id   = arg->card_id; 
        open_connection_card(card_id);
        pthread_exit(0);
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

int setup_detector(sls::Detector *det) {
        det->stopDetector();

        det->setStartingFrameNumber(1);
        det->setNumberOfFrames(experiment_settings.nframes_to_collect + DELAY_FRAMES_STOP_AND_QUIT+1);
        if (det->size() != NMODULES * NCARDS) {
            std::cerr << "Mismatch in detector size" << std::endl;
            return 1;
        }
        experiment_settings.frame_time = det->getPeriod().squash().count() / (1000.0 * 1000.0 * 1000.0);
        experiment_settings.count_time = det->getExptime().squash().count() / (1000.0 * 1000.0 * 1000.0);
        std::cout << "JUNGFRAU Frame time: " << experiment_settings.frame_time << " s   Exp time:" << experiment_settings.count_time << " s" << std::endl;

        if (experiment_settings.conversion_mode == MODE_PEDEG1) det->setSettings(slsDetectorDefs::detectorSettings::FORCESWITCHG1);
        else if (experiment_settings.conversion_mode == MODE_PEDEG2) det->setSettings(slsDetectorDefs::detectorSettings::FORCESWITCHG2);
        else det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);

        if (writer_settings.timing_trigger)
            det->setTimingMode(slsDetectorDefs::timingMode::TRIGGER_EXPOSURE);
        else
            det->setTimingMode(slsDetectorDefs::timingMode::AUTO_TIMING);
        
        return 0;
}

int trigger_detector(sls::Detector *det) {
        if (writer_settings.timing_trigger) {
            det->startDetector();
            // sleep 200 ms is necessary for SNAP setup
            // TODO: Likely 20-50 us would be enough
            usleep(200000);
            trigger_omega();
        } else {
            usleep(200000);
            det->startDetector();
        } 
        return 0;
}

int close_detector(sls::Detector *det) {
        det->stopDetector();
        det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);
        return 0;
}

int main(int argc, char **argv) {
	int ret;

      	// Parse input
	if (parse_input(argc, argv) == 1) exit(EXIT_FAILURE);

        // Register HDF5 bitshuffle filter
        H5Zregister(bshuf_H5Filter);

	for (int i = 0; i < NCARDS; i++) {
            allocate_memory(i);
            open_connection_card(i);
	    remaining_frames[i] = experiment_settings.nframes_to_write;
	    pthread_mutex_init(&(remaining_frames_mutex[i]), NULL);
	}

        if (experiment_settings.conversion_mode == 255) {
            exit(EXIT_SUCCESS);
        }

        // Configure detector
#ifndef OFFLINE
        sls::Detector det;
        if (setup_detector(&det) == 1) exit(EXIT_FAILURE);
#endif

	pthread_t writer[writer_settings.nthreads];
	writer_thread_arg_t writer_thread_arg[writer_settings.nthreads];

        open_data_hdf5();

        // Barrier #1 - All threads are set up running
	for (int i = 0; i < NCARDS; i++)
            exchange_magic_number(writer_connection_settings[i].sockfd);

	if (experiment_settings.nframes_to_write > 0) {
		for (int i = 0; i < writer_settings.nthreads; i++) {
			writer_thread_arg[i].thread_id = i / NCARDS;
                        if (NCARDS > 1) 
			    writer_thread_arg[i].card_id = i % NCARDS;
                        else
                            writer_thread_arg[i].card_id = 0;
			ret = pthread_create(&(writer[i]), NULL, writer_thread, &(writer_thread_arg[i]));
		}
                std::cout << "Threads started" << std::endl;
        }

        // Barrier #2 - All threads are set up running
	for (int i = 0; i < NCARDS; i++)
            exchange_magic_number(writer_connection_settings[i].sockfd);
	
        auto start = std::chrono::system_clock::now();

#ifndef OFFLINE
        trigger_detector(&det);
#endif

	if (experiment_settings.nframes_to_write > 0) {
		for (int i = 0; i < writer_settings.nthreads; i++)
			ret = pthread_join(writer[i], NULL);

		auto end = std::chrono::system_clock::now();

		std::chrono::duration<double> diff = end - start;

		std::cout << "Throughput: " << (float) (experiment_settings.nframes_to_write) / diff.count() << " frames/s" << std::endl;
		std::cout << "Compression ratio: " << ((double) total_compressed_size * 8) / ((double) NPIXEL * (double) NCARDS * (double) experiment_settings.nframes_to_write) << " bits/pixel" <<std::endl;
	}

        // Involves barrier after collecting data
	for (int i = 0; i < NCARDS; i++) close_connection_card(i);

#ifndef OFFLINE        
        close_detector(&det);
#endif

	if (writer_settings.HDF5_prefix != "") {

		save_gain_pedestal_hdf5();
                save_master_hdf5();
        }
	// Only save pedestal and gain, if filename provided
        if (!writer_settings.write_hdf5) pack_data_hdf5();

        close_data_hdf5();
        std::cout << " <<< DONE >>> " << std::endl;
}
