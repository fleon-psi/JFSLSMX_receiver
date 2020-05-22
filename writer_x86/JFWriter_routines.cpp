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

#include "JFWriter.h"
#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"

int jfwriter_setup() {
        // Register HDF5 bitshuffle filter
        H5Zregister(bshuf_H5Filter);
#ifndef OFFLINE
        det = new sls::Detector();
#endif
	for (int i = 0; i < NCARDS; i++) {
            // Setup IB and allocate memory
            setup_infiniband(i);
	    pthread_mutex_init(&(remaining_frames_mutex[i]), NULL);
	}
        return 0;
}

int jfwriter_close() {
	for (int i = 0; i < NCARDS; i++) {
            // Setup IB and allocate memory
            close_infiniband(i);
	    pthread_mutex_destroy(&(remaining_frames_mutex[i]));
	}
#ifndef OFFLINE
        delete(det);
#endif
        return 0;
}

int jfwriter_arm() {
	for (int i = 0; i < NCARDS; i++) {
            connect_to_power9(i);
	    remaining_frames[i] = experiment_settings.nframes_to_write;
	}

	if (experiment_settings.conversion_mode == MODE_QUIT)
            return 0;
#ifndef OFFLINE
        if (setup_detector() == 1) return 1;
#endif
        if (writer_settings.write_hdf5 == true)
            open_data_hdf5();

        writer = (pthread_t *) calloc(writer_settings.nthreads, sizeof(pthread_t));
        writer_thread_arg = (writer_thread_arg_t *) calloc(writer_settings.nthreads, sizeof(writer_thread_arg_t));

        // Barrier #1 - All threads on P9 are set up running
	for (int i = 0; i < NCARDS; i++)
            exchange_magic_number(writer_connection_settings[i].sockfd);

	if (experiment_settings.nframes_to_write > 0) {
		for (int i = 0; i < writer_settings.nthreads; i++) {
			writer_thread_arg[i].thread_id = i / NCARDS;
                        if (NCARDS > 1) 
			    writer_thread_arg[i].card_id = i % NCARDS;
                        else
                            writer_thread_arg[i].card_id = 0;
			int ret = pthread_create(&(writer[i]), NULL, writer_thread, &(writer_thread_arg[i]));
		}
                std::cout << "Threads started" << std::endl;
        }

        // Barrier #2 - All threads are set up running
	for (int i = 0; i < NCARDS; i++)
            exchange_magic_number(writer_connection_settings[i].sockfd);
	
#ifndef OFFLINE
        trigger_detector();
#endif
        if ((experiment_settings.pedestalG0_frames > 0) && (experiment_settings.conversion_mode == MODE_CONV))
            time_pedestalG0 = time_datacollection;
        return 0;
}

int jfwriter_disarm() {
	if (experiment_settings.conversion_mode == MODE_QUIT)
            return 0;

	if (experiment_settings.nframes_to_write > 0) {
		for (int i = 0; i < writer_settings.nthreads; i++)
			int ret = pthread_join(writer[i], NULL);
	}

        // Data files can be closed, when all frames were written,
        // even if collection is still running
        if (writer_settings.write_hdf5 == true)
            close_data_hdf5();

        // Involves barrier after collecting data
	for (int i = 0; i < NCARDS; i++)
            disconnect_from_power9(i);
#ifndef OFFLINE        
        close_detector();
#endif
	if (writer_settings.HDF5_prefix != "")
            save_master_hdf5();
        return 0;
}

void mean_pedeG0(double out[NMODULES*NCARDS]) {
    for (size_t i = 0; i < NMODULES * NCARDS; i++) {
        double sum = 0;
        double count = 0;
        for (size_t j = 0; j < MODULE_COLS*MODULE_LINES; j++) {
           if (gain_pedestal.pixel_mask[i * MODULE_COLS * MODULE_LINES + j] == 0) {
               sum += gain_pedestal.pedeG0[i * MODULE_COLS * MODULE_LINES + j] / 4.0;
               count += 1.0;
           }
        }
        out[i] = sum / count;
    }
}

void mean_pedeG1(double out[NMODULES*NCARDS]) {
    for (size_t i = 0; i < NMODULES * NCARDS; i++) {
        double sum = 0;
        double count = 0;
        for (size_t j = 0; j < MODULE_COLS*MODULE_LINES; j++) {
           if (gain_pedestal.pixel_mask[i * MODULE_COLS * MODULE_LINES + j] == 0) {
               sum += gain_pedestal.pedeG1[i * MODULE_COLS * MODULE_LINES + j] / 4.0;
               count += 1.0;
           }
        }
        out[i] = sum / count;
    }
}

void mean_pedeG2(double out[NMODULES*NCARDS]) {
    for (size_t i = 0; i < NMODULES * NCARDS; i++) {
        double sum = 0;
        double count = 0;
        for (size_t j = 0; j < MODULE_COLS*MODULE_LINES; j++) {
           if (gain_pedestal.pixel_mask[i * MODULE_COLS * MODULE_LINES + j] == 0) {
               sum += gain_pedestal.pedeG2[i * MODULE_COLS * MODULE_LINES + j] / 4.0;
               count += 1.0;
           }
        }
        out[i] = sum / count;
    }
}

void count_bad_pixel(size_t out[NMODULES*NCARDS]) {
    for (size_t i = 0; i < NMODULES * NCARDS; i++) {
        size_t count = 0;
        for (size_t j = 0; j < MODULE_COLS*MODULE_LINES; j++) {
           if (gain_pedestal.pixel_mask[i * MODULE_COLS * MODULE_LINES + j] != 0)
              count ++;           
        }
        out[i] = count;
    }
}

int jfwriter_pedestal_G0() {
    experiment_settings_t tmp_settings = experiment_settings;
    experiment_settings.nframes_to_collect = experiment_settings.pedestalG0_frames;
    experiment_settings.nframes_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.conversion_mode = MODE_PEDEG0;
    writer_settings.timing_trigger = false;

    if (jfwriter_arm() == 1) return 1;
    if (jfwriter_disarm() == 1) return 1;
    time_pedestalG0 = time_datacollection;    

    experiment_settings = tmp_settings;
    return 0;
}

int jfwriter_pedestal_G1() {
    experiment_settings_t tmp_settings = experiment_settings;
    experiment_settings.nframes_to_collect = experiment_settings.pedestalG1_frames;
    experiment_settings.nframes_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.conversion_mode = MODE_PEDEG1;
    writer_settings.timing_trigger = false;

    if (jfwriter_arm() == 1) return 1;
    if (jfwriter_disarm() == 1) return 1;

    time_pedestalG1 = time_datacollection;    
    
    experiment_settings = tmp_settings;
    return 0;
}

int jfwriter_pedestal_G2() {
    experiment_settings_t tmp_settings = experiment_settings;
    experiment_settings.nframes_to_collect = experiment_settings.pedestalG2_frames;
    experiment_settings.nframes_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.conversion_mode = MODE_PEDEG2;
    writer_settings.timing_trigger = false;

    if (jfwriter_arm() == 1) return 1;
    if (jfwriter_disarm() == 1) return 1;

    time_pedestalG2 = time_datacollection;
    
    experiment_settings = tmp_settings;
    return 0;
}
