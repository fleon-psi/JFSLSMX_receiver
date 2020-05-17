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

#include "JFWriter.h"
#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"

// Taken from bshuf
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
			experiment_settings.conversion_mode = MODE_QUIT;
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

int main(int argc, char **argv) {
      	// Parse input
	if (parse_input(argc, argv) == 1) exit(EXIT_FAILURE);

        jfwriter_setup();
        jfwriter_arm();

        auto start = std::chrono::system_clock::now();

        jfwriter_disarm();

	if (experiment_settings.nframes_to_write > 0) {
		auto end = std::chrono::system_clock::now();

		std::chrono::duration<double> diff = end - start;

		std::cout << "Throughput: " << (float) (experiment_settings.nframes_to_write) / diff.count() << " frames/s" << std::endl;
		std::cout << "Compression ratio: " << ((double) total_compressed_size * 8) / ((double) NPIXEL * (double) NCARDS * (double) experiment_settings.nframes_to_write) << " bits/pixel" <<std::endl;
	}

        jfwriter_close();

        std::cout << " <<< DONE >>> " << std::endl;
}
