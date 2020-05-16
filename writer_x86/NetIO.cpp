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

#include <endian.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "JFWriter.h"

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

int open_connection_card(int card_id) {
	if (TCP_connect(writer_connection_settings[card_id].sockfd,
			writer_connection_settings[card_id].receiver_host,
			writer_connection_settings[card_id].receiver_tcp_port) == 1)
		return 1;

	// Provide experiment settings to receiver
	send(writer_connection_settings[card_id].sockfd,
			&experiment_settings, sizeof(experiment_settings_t), 0);

	if (experiment_settings.conversion_mode == MODE_EXIT) {
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

int close_connection_card(int card_id) {
        if (experiment_settings.conversion_mode == MODE_EXIT) {
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
