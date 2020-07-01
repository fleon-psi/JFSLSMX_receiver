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

#include <fstream>
#include <iostream>
#include <infiniband/verbs.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "JFReceiver.h"

int parse_input(int argc, char **argv) {
    int opt;
#ifdef OCACCEL
    receiver_settings.card_number = 4;
#else
    receiver_settings.card_number = 0;
#endif
    receiver_settings.compression_threads = 2;
    receiver_settings.ib_dev_name = "mlx5_0";
    receiver_settings.fpga_mac_addr = 0xAABBCCDDEEF1;
    receiver_settings.fpga_ip_addr = 0x0A013205;
    receiver_settings.tcp_port = 52320;
    receiver_settings.pedestal_file_name = "pedestal_card0.dat";
    receiver_settings.gpu_device = 0;

    receiver_settings.gain_file_name[0] =
            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M352_2020-01-31.bin";
    receiver_settings.gain_file_name[1] =
            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M307_2020-01-20.bin";
    receiver_settings.gain_file_name[2] =
            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M264_2019-07-29.bin";
    receiver_settings.gain_file_name[3] =
            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M253_2019-07-29.bin";

    while ((opt = getopt(argc,argv,":C:t:I:P:p:0:1:2:3:G")) != EOF)
        switch(opt)
        {
            case 'C':
                receiver_settings.card_number = atoi(optarg);
                //TODO: Only update if no values provided
                if  (receiver_settings.card_number == 1) {
#ifdef OCACCEL
                    receiver_settings.card_number = 5;
#endif
                    receiver_settings.ib_dev_name = "mlx5_2";
                    receiver_settings.fpga_mac_addr = 0xAABBCCDDEEF2;
                    receiver_settings.fpga_ip_addr = 0x0A013206;
                    receiver_settings.tcp_port = 52321;
                    receiver_settings.pedestal_file_name = "pedestal_card1.dat";
                    receiver_settings.gpu_device = 1;
                    receiver_settings.gain_file_name[0] =
                            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M351_2020-01-20.bin";
                    receiver_settings.gain_file_name[1] =
                            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M312_2020-01-20.bin";
                    receiver_settings.gain_file_name[2] =
                            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M261_2019-07-29.bin";
                    receiver_settings.gain_file_name[3] =
                            "/home/jungfrau/JF4M_X06SA_200511/gainMaps_M373_2020-01-31.bin";
                }
                break;
            case 't':
                receiver_settings.compression_threads = atoi(optarg);
                break;
            case 'I':
                receiver_settings.ib_dev_name = std::string(optarg);
                break;
            case 'P':
                receiver_settings.tcp_port = atoi(optarg);
                break;
            case 'p':
                receiver_settings.pedestal_file_name = std::string(optarg);
                break;
            case 0:
                receiver_settings.gain_file_name[0] = std::string(optarg);
                break;
            case 1:
                receiver_settings.gain_file_name[1] = std::string(optarg);
                break;
            case 2:
                receiver_settings.gain_file_name[2] = std::string(optarg);
                break;
            case 3:
                receiver_settings.gain_file_name[3] = std::string(optarg);
                break;
        }
    return 0;
}

int allocate_memory() {
    frame_buffer_size       = FRAME_BUF_SIZE * NPIXEL * sizeof(int16_t); // can store FRAME_BUF_SIZE frames
    status_buffer_size      = FRAME_LIMIT*NMODULES*128/8+64;   // can store 1 bit per each ETH packet expected
    gain_pedestal_data_size = 7 * 2 * NPIXEL;  // each entry to in_parameters_array is 2 bytes and there are 6 constants per pixel + mask
    jf_packet_headers_size  = FRAME_LIMIT * NMODULES * sizeof(header_info_t);
    ib_buffer_size          = COMPOSED_IMAGE_SIZE * RDMA_SQ_SIZE * sizeof(int16_t);

    // Arrays are allocated with mmap for the higest possible performance. Output is page aligned, so it will be also 64b aligned.
    frame_buffer       = (int16_t *)  mmap (NULL, frame_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0) ;
    status_buffer      = (char *) mmap (NULL, status_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    gain_pedestal_data = (uint16_t *) mmap (NULL, gain_pedestal_data_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    jf_packet_headers  = (header_info_t *) mmap (NULL, jf_packet_headers_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    ib_buffer          = (char *) mmap (NULL, ib_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);

    if ((frame_buffer == NULL) || (status_buffer == NULL) ||
        (gain_pedestal_data == NULL) || (jf_packet_headers == NULL) ||
        (ib_buffer == NULL)) {
        std::cerr << "Memory allocation error" << std::endl;
        return 1;
    }

    // Fill output arrays with zeros
    memset(frame_buffer, 0x0, frame_buffer_size);
    memset(status_buffer, 0x0, status_buffer_size);
    memset(gain_pedestal_data, 0x0, gain_pedestal_data_size);
    memset(jf_packet_headers, 0x0, jf_packet_headers_size);
    memset(ib_buffer, 0x0, ib_buffer_size);

    packet_counter = (char *) (status_buffer + 64);
    online_statistics = (online_statistics_t *) status_buffer;

    return 0;
}

void deallocate_memory() {
    munmap(frame_buffer, frame_buffer_size);
    munmap(status_buffer, status_buffer_size);
    munmap(gain_pedestal_data, gain_pedestal_data_size);
    munmap(jf_packet_headers, jf_packet_headers_size);
    munmap(ib_buffer, ib_buffer_size);
}

int load_bin_file(std::string fname, char *dest, size_t size) {
//	std::cout << "Loading " << fname.c_str() << std::endl;
    std::fstream file10(fname.c_str(), std::fstream::in | std::fstream::binary);
    if (!file10.is_open()) {
        std::cerr << "Error opening file " << fname.c_str() << std::endl;
        return 1;
    } else {
        file10.read(dest, size);
        file10.close();
        return 0;
    }
}

void load_gain(std::string fname, int module, double energy_in_keV) {
    double *tmp_gain = (double *) calloc (3 * MODULE_COLS * MODULE_LINES, sizeof(double));
    load_bin_file(fname, (char *) tmp_gain, 3 * MODULE_COLS * MODULE_LINES * sizeof(double));

    size_t offset = module * MODULE_COLS * MODULE_LINES;

    // 14-bit fractional part
    for (int i = 0; i < MODULE_COLS * MODULE_LINES; i ++)
        gain_pedestal_data[offset+i] =  (uint16_t) ((512.0 / (tmp_gain[i] * energy_in_keV)) * 16384 + 0.5);

    // 13-bit fractional part
    offset += NPIXEL;
    for (int i = 0; i < MODULE_COLS * MODULE_LINES; i ++)
        gain_pedestal_data[offset+i] =  (uint16_t) (-1.0 / ( tmp_gain[i + MODULE_COLS * MODULE_LINES] * energy_in_keV) * 8192 + 0.5);

    // 13-bit fractional part
    offset += NPIXEL;
    for (int i = 0; i < MODULE_COLS * MODULE_LINES; i ++)
        gain_pedestal_data[offset+i] =  (uint16_t) (-1.0 / ( tmp_gain[i + 2 * MODULE_COLS * MODULE_LINES] * energy_in_keV) * 8192 + 0.5);

    free(tmp_gain);
}

// Loads pedestal and pixel mask
void load_pedestal(std::string fname) {
    load_bin_file(fname, (char *)(gain_pedestal_data + 3 * NPIXEL), 4 * NPIXEL * sizeof(uint16_t));
}

// Saves pedestal and pixel mask
void save_pedestal(std::string fname) {
    std::cout << "Saving " << fname.c_str() << std::endl;
    std::fstream file10(fname.c_str(), std::ios::out | std::ios::binary);
    if (!file10.is_open()) {
        std::cerr << "Error opening file " << fname.c_str() << std::endl;
    } else {
        file10.write((char *) (gain_pedestal_data + 3 * NPIXEL), 4 * NPIXEL * sizeof(uint16_t));
        file10.close();
    }
}

void update_bad_pixel_list() {
    bad_pixels.clear();
    for (int i = 0; i < NPIXEL; i++) {
        if (gain_pedestal_data[6*NPIXEL+i] != 0) {
            size_t module = i / (MODULE_LINES * MODULE_COLS);
            size_t line = (i / MODULE_LINES) % MODULE_COLS;
            size_t column = i % (MODULE_COLS * MODULE_LINES);
            uint16_t line_out = (513L - line - (line / 256) * 2L + 514L * (3 - (module / 2))) * 2L + module % 2;
            uint16_t column_out = column + (column / 256) * 2L;
            bad_pixels.insert(std::pair<int16_t, int16_t>(column_out, line_out));
        }
    }
}


// Establish TCP/IP server to communicate with writer
int TCP_server(uint16_t port) {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == 0) {
        std::cerr << "TCP/IP socket error" << std::endl;
        exit(EXIT_FAILURE);
        return 1;
    }

    struct sockaddr_in srv_address;
    srv_address.sin_family = AF_INET;
    srv_address.sin_addr.s_addr = INADDR_ANY;
    srv_address.sin_port = htons( port );

    if (bind(sockfd, (struct sockaddr *)&srv_address, sizeof(srv_address)) < 0) {
        std::cerr << "TCP/IP bind error" << std::endl;
        return 1;
    }

    listen(sockfd, 1);
    std::cout << "Listening on TCP/IP port " << port << std::endl;
    return 0;
}

int TCP_exchange_magic_number() {
    uint64_t magic_number = TCPIP_CONN_MAGIC_NUMBER;

    // Send parameters
    send(accepted_socket, &magic_number, sizeof(uint64_t), 0);
    // Receive parameters
    read(accepted_socket, &magic_number, sizeof(uint64_t));

    if (magic_number != TCPIP_CONN_MAGIC_NUMBER) {
        std::cerr << "Mismatch in TCP/IP communication" << std::endl;
        return 1;
    }
    return 0;
}

int TCP_accept_connection() {
    struct sockaddr_in client_address;
    // Accept TCP/IP connection
    socklen_t addrlen = sizeof(client_address);
    accepted_socket = accept(sockfd, (struct sockaddr *)&client_address, &addrlen);
    return TCP_exchange_magic_number();
}

void TCP_close_connection() {
    close(sockfd);
}

void TCP_exchange_IB_parameters(ib_comm_settings_t *remote) {
    ib_comm_settings_t local;
    local.qp_num = ib_settings.qp->qp_num;
    local.dlid = ib_settings.port_attr.lid;
    local.rq_psn = RDMA_SQ_PSN;

    // Send parameters
    send(accepted_socket, &local, sizeof(ib_comm_settings_t), 0);

    // Receive parameters
    read(accepted_socket, remote, sizeof(ib_comm_settings_t));
}

int TCP_receive(int sockfd, char *buffer, size_t size) {
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

int main(int argc, char **argv) {
    int ret;

    std::cout << "JF Receiver " << std::endl;

    // Parse input parameters
    if (parse_input(argc, argv) == 1) exit(EXIT_FAILURE);

    // Allocate memory
    if (allocate_memory() == 1) exit(EXIT_FAILURE);
    std::cout << "Memory allocated" << std::endl;

    // Load pedestal file
    load_pedestal(receiver_settings.pedestal_file_name);

    // Load test data
#ifdef RECEIVE_FROM_FILE
    std::ifstream ifile("output_data.dat", std::ios::binary | std::ios::in);
        ifile.read((char *) frame_buffer, NPIXEL * sizeof(uint16_t) * FRAME_BUF_SIZE);
        ifile.close();
#endif
    // Establish RDMA link
    if (setup_ibverbs(ib_settings, receiver_settings.ib_dev_name.c_str(), RDMA_SQ_SIZE, 0) == 1) exit(EXIT_FAILURE);
    std::cout << "IB link ready" << std::endl;

    // Register memory regions
    ib_settings.buffer_mr = ibv_reg_mr(ib_settings.pd, ib_buffer, ib_buffer_size, 0);
    if (ib_settings.buffer_mr == NULL) {
        std::cerr << "Failed to register IB memory region." << std::endl;
        return 1;
    }

    // Allocate space on GPU
    if (setup_gpu(receiver_settings.gpu_device) == 1) exit(EXIT_FAILURE);

    // Establish TCP/IP server
    if (TCP_server(receiver_settings.tcp_port) == 1) exit(EXIT_FAILURE);

#ifndef RECEIVE_FROM_FILE
    // Connect to FPGA board
    if (setup_snap(receiver_settings.card_number) == 1) exit(EXIT_FAILURE);
#endif
    while (1) {
        // Accept TCP/IP communication
        while (TCP_accept_connection() != 0)
            std::cout << "Bogus TCP/IP connection" << std::endl;

        // Receive experimental settings via TCP/IP
        TCP_receive(accepted_socket, (char *) &experiment_settings, sizeof(experiment_settings_t));
        if (experiment_settings.conversion_mode == 255) {
            std::cout << "Exiting" << std::endl;
            break;
        }

        // Exchange IB information
        ib_comm_settings_t remote;
        TCP_exchange_IB_parameters(&remote);

        // Switch to ready to send state for IB
        if (switch_to_rtr(ib_settings, 0, remote.dlid, remote.qp_num) == 1) exit(EXIT_FAILURE);
        std::cout << "IB Ready to receive" << std::endl;
        if (switch_to_rts(ib_settings, RDMA_SQ_PSN) == 1) exit(EXIT_FAILURE);
        std::cout << "IB Ready to send" << std::endl;
        std::cout << "Pixel depth " << experiment_settings.pixel_depth << " byte" << std::endl;
        std::cout << "Energy: " << experiment_settings.energy_in_keV << " keV" << std::endl;
        std::cout << "Images to write: " << experiment_settings.nimages_to_write << std::endl;
        std::cout << "Summation: " << experiment_settings.summation << std::endl;
        std::cout << "Spot finding enabled: " << experiment_settings.enable_spot_finding << std::endl;

        memset(ib_buffer_occupancy, 0, RDMA_SQ_SIZE * sizeof(uint16_t));

        for (int i = 0; i < NMODULES; i++)
            online_statistics->head[i] = 0;

        // TODO: Load gain before, only multiply by energy here
        // TODO: Multi-pixels divided by two/four in gain calculation
        // Load gain files (double, per module)
        for (int i = 0; i < NMODULES; i++)
            load_gain(receiver_settings.gain_file_name[i], i, experiment_settings.energy_in_keV);

        // Barrier #1
        TCP_exchange_magic_number();

        pthread_t poll_cq_thread;
        pthread_t snap_thread;
        pthread_t gpu_thread[NCUDA_STREAMS];
        pthread_t send_thread[receiver_settings.compression_threads];
        ThreadArg gpu_thread_arg[NCUDA_STREAMS];
        ThreadArg send_thread_arg[receiver_settings.compression_threads];


        // Reset counter for GPU synchronization
        if (experiment_settings.enable_spot_finding) {
            for (int i = 0; i < NCUDA_STREAMS*CUDA_TO_IB_BUFFER; i++) {
                writer_threads_done[i] = 0;
                cuda_stream_ready[i]   = i;
            }
            for (int i = 0; i < NCUDA_STREAMS; i++) {
                gpu_thread_arg[i].ThreadID = i;
                ret = pthread_create(gpu_thread+i, NULL, run_gpu_thread, gpu_thread_arg+i);
                PTHREAD_ERROR(ret,pthread_create);
            }
        }

        for (int i = 0; i < receiver_settings.compression_threads ; i++) {
            send_thread_arg[i].ThreadID = i;
            ret = pthread_create(send_thread+i, NULL, run_send_thread, send_thread_arg+i);
            PTHREAD_ERROR(ret,pthread_create);
        }

        ret = pthread_create(&poll_cq_thread, NULL, run_poll_cq_thread, NULL);
        PTHREAD_ERROR(ret, pthread_create);

        // Just for test - set collected frames to expected
#ifdef RECEIVE_FROM_FILE
        for (int i = 0; i < NMODULES; i++)
              online_statistics->head[i] = experiment_settings.nframes_to_collect - 1;
           online_statistics->good_packets = NMODULES * 128 * experiment_settings.nframes_to_collect;
#endif

#ifndef RECEIVE_FROM_FILE
        // Start SNAP thread
        ret = pthread_create(&snap_thread, NULL, run_snap_thread, NULL);
        PTHREAD_ERROR(ret,pthread_create);
#endif

        // Barrier #2
        TCP_exchange_magic_number();

        // Check for GPU thread completion
        if (experiment_settings.enable_spot_finding) {
            for (int i = 0; i < NCUDA_STREAMS; i++) {
                ret = pthread_join(gpu_thread[i], NULL);
                PTHREAD_ERROR(ret, pthread_join);
            }
        }

        // Check for thread completion
        ret = pthread_join(poll_cq_thread, NULL);
        PTHREAD_ERROR(ret, pthread_join);

        // Check for sending threads completion
        for	(int i = 0; i <	receiver_settings.compression_threads ; i++) {
            ret = pthread_join(send_thread[i], NULL);
            PTHREAD_ERROR(ret,pthread_join);
        }

        // Check for SNAP thread completion
#ifndef RECEIVE_FROM_FILE
        ret = pthread_join(snap_thread, NULL);
        PTHREAD_ERROR(ret, pthread_join);
#endif

        // Print some quick statistics
        std::cout << "Good packets " << online_statistics->good_packets << " Frames to collect: " << experiment_settings.nframes_to_collect << std::endl;

        // For conversion only packets contributing to images are written
        if ((experiment_settings.conversion_mode == MODE_CONV) && (experiment_settings.ntrigger > 0))
            std::cout << "Images collected " << ((double)(online_statistics->good_packets / NMODULES / 128)) / (double) (experiment_settings.nimages_to_write * experiment_settings.summation) * 100.0 << "%" << std::endl;
        else
            std::cout << "Frames collected " << ((double)(online_statistics->good_packets / NMODULES / 128)) / (double) experiment_settings.nframes_to_collect * 100.0 << "%" << std::endl;

        // Send header data and collection statistics
        send(accepted_socket, online_statistics, sizeof(online_statistics_t), 0);
        // Send gain, pedestal and pixel mask
        send(accepted_socket, gain_pedestal_data, 7*NPIXEL*sizeof(uint16_t), 0);
        // Update bad pixel pixel list for spot finding;
        update_bad_pixel_list();
        // Reset QP
        switch_to_reset(ib_settings);
        switch_to_init(ib_settings);

        // Barrier
        // Check magic number again
        TCP_exchange_magic_number();
        close(accepted_socket);

        // Reset status buffer
        memset(status_buffer, 0x0, status_buffer_size);
    }

#ifndef RECEIVE_FROM_FILE
    // Close SNAP
    close_snap();
#endif
    // Close GPU
    close_gpu();

    // Save pedestal
    save_pedestal(receiver_settings.pedestal_file_name);

    // Disconnect client
    close(accepted_socket);

    // Deregister memory region
    ibv_dereg_mr(ib_settings.buffer_mr);

    // Close RDMA
    close_ibverbs(ib_settings);

    // Close TCP/IP socket
    TCP_close_connection();

    // Deallocate memory
    deallocate_memory();

    // Quit peacefully
    std::cout << "JFReceiver Done" << std::endl;
}
