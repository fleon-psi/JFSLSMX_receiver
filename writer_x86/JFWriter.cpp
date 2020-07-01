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

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/mman.h>
#include <algorithm>

#include "JFWriter.h"
#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"
#include "../include/xray.h"

double mean_pedestalG0[NMODULES*NCARDS];
double mean_pedestalG1[NMODULES*NCARDS];
double mean_pedestalG2[NMODULES*NCARDS];
size_t bad_pixels[NMODULES*NCARDS];

int jfwriter_setup() {
    // Register HDF5 bitshuffle filter
    H5Zregister(bshuf_H5Filter);

    // Setup
#ifndef OFFLINE
    try {
        det = new sls::Detector();
    } catch (const std::runtime_error& error) {
        return 1;
    }
#endif
    for (int i = 0; i < NCARDS; i++) {
        // Setup IB and allocate memory
        if (setup_infiniband(i)) return 1;
        pthread_mutex_init(&(remaining_images_mutex[i]), NULL);
    }

    init_influxdb_client();

    return 0;
}

int jfwriter_close() {
    for (int i = 0; i < NCARDS; i++) {
        // Setup IB and allocate memory
        close_infiniband(i);
        pthread_mutex_destroy(&(remaining_images_mutex[i]));
    }
    close_influxdb_client();

#ifndef OFFLINE
    delete(det);
#endif
    return 0;
}


// Start and stop are low level procedures to execute any measurement
// Arm, disarm and pedestalG0/1/2 are wrappers for actual tasks

int jfwriter_start() {
    for (int i = 0; i < NCARDS; i++) {
        if (connect_to_power9(i)) return 1;
        remaining_images[i] = experiment_settings.nimages_to_write;
    }

    if (experiment_settings.conversion_mode == MODE_QUIT)
        return 0;

    // Reset compressed dataset size
    total_compressed_size = 0;

#ifndef OFFLINE
    if (setup_detector() == 1) return 1;
#endif

    writer_thread = (pthread_t *) calloc(writer_settings.nthreads, sizeof(pthread_t));
    writer_thread_arg = (writer_thread_arg_t *) calloc(writer_settings.nthreads, sizeof(writer_thread_arg_t));

    // Barrier #1 - All threads on P9 are set up running
    for (int i = 0; i < NCARDS; i++)
        if (exchange_magic_number(writer_connection_settings[i].sockfd)) return 1;

    // Start writer threads - these threads receive images via IB Verbs
    if (experiment_settings.nimages_to_write > 0) {
        if (writer_settings.write_mode == JF_WRITE_HDF5)
            if (open_data_hdf5()) return 1;

        for (int i = 0; i < writer_settings.nthreads; i++) {
            writer_thread_arg[i].thread_id = i / NCARDS;
            if (NCARDS > 1)
                writer_thread_arg[i].card_id = i % NCARDS;
            else
                writer_thread_arg[i].card_id = 0;
            int ret = pthread_create(writer_thread+i, NULL, run_writer_thread, writer_thread_arg+i);
        }
    }

    // Start metadata threads - these threads receive metadata via TCP/IP socket
    // When started, these threads will exchange magic number again (barrier #2)
    for (int i = 0; i < NCARDS; i++) {
        metadata_thread_arg[i].card_id = i;
        int ret = pthread_create(metadata_thread+i, NULL, run_metadata_thread, metadata_thread_arg+i);
    }

#ifndef OFFLINE
    trigger_detector();
#endif
    clock_gettime(CLOCK_REALTIME, &time_start);

    if ((experiment_settings.pedestalG0_frames > 0) && (experiment_settings.conversion_mode == MODE_CONV))
        time_pedestalG0 = time_start;
    return 0;
}

int jfwriter_stop() {
    if (experiment_settings.conversion_mode == MODE_QUIT)
        return 0;

    if (experiment_settings.nimages_to_write > 0) {
        for (int i = 0; i < writer_settings.nthreads; i++)
            int ret = pthread_join(writer_thread[i], NULL);

        // Data files can be closed, when all frames were written,
        // even if collection is still running

        if (writer_settings.write_mode == JF_WRITE_HDF5)
            close_data_hdf5();
    }
    // Record end time, as time when everything has ended
    clock_gettime(CLOCK_REALTIME, &time_end);

    // Involves barrier after collecting data
    for (int i = 0; i < NCARDS; i++) {
        int ret = pthread_join(metadata_thread[i], NULL);
        if (disconnect_from_power9(i)) return 1;
    }

#ifndef OFFLINE
    close_detector();
#endif

    return 0;
}

void calc_mean_pedestal(uint16_t in[NCARDS*NPIXEL], double out[NMODULES*NCARDS]) {
    for (size_t i = 0; i < NMODULES * NCARDS; i++) {
        double sum = 0;
        double count = 0;
        for (size_t j = 0; j < MODULE_COLS*MODULE_LINES; j++) {
            if (gain_pedestal.pixel_mask[i * MODULE_COLS * MODULE_LINES + j] == 0) {
                sum += in[i * MODULE_COLS * MODULE_LINES + j] / 4.0;
                count += 1.0;
            }
        }
        out[i] = sum / count;
    }
}

void count_bad_pixel() {
    for (size_t i = 0; i < NMODULES * NCARDS; i++) {
        size_t count = 0;
        for (size_t j = 0; j < MODULE_COLS*MODULE_LINES; j++) {
            if (gain_pedestal.pixel_mask[i * MODULE_COLS * MODULE_LINES + j] != 0)
                count ++;
        }
        bad_pixels[i] = count;
    }
}

int jfwriter_pedestalG0() {
    sleep(1); // Added to ensure that there is enough time between pedestal measurements
    experiment_settings_t tmp_settings = experiment_settings;
    experiment_settings.nframes_to_collect = experiment_settings.pedestalG0_frames;

    experiment_settings.nimages_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.conversion_mode = MODE_PEDEG0;
    writer_settings.timing_trigger = true;

    if (jfwriter_start() == 1) return 1;
    if (jfwriter_stop() == 1) return 1;
    time_pedestalG0 = time_start;

    experiment_settings = tmp_settings;
    calc_mean_pedestal(gain_pedestal.pedeG0, mean_pedestalG0);
    count_bad_pixel();
    log_pedestal_G0();
    sleep(1); // Just for safety
    return 0;
}

int jfwriter_pedestalG1() {
    sleep(1); // Added to ensure that there is enough time between pedestal measurements
    experiment_settings_t tmp_settings = experiment_settings;
    experiment_settings.nframes_to_collect = experiment_settings.pedestalG1_frames;
    experiment_settings.nimages_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.conversion_mode = MODE_PEDEG1;
    writer_settings.timing_trigger = true;

    if (jfwriter_start() == 1) return 1;
    if (jfwriter_stop() == 1) return 1;
    time_pedestalG1 = time_start;

    experiment_settings = tmp_settings;
    calc_mean_pedestal(gain_pedestal.pedeG1, mean_pedestalG1);
    count_bad_pixel();
    log_pedestal_G1();
    sleep(1); // Just for safety
    return 0;
}

int jfwriter_pedestalG2() {
    sleep(1); // Added to ensure that there is enough time between pedestal measurements
    experiment_settings_t tmp_settings = experiment_settings;
    experiment_settings.nframes_to_collect = experiment_settings.pedestalG2_frames;
    experiment_settings.nimages_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.conversion_mode = MODE_PEDEG2;
    writer_settings.timing_trigger = true;

    if (jfwriter_start() == 1) return 1;
    if (jfwriter_stop() == 1) return 1;

    time_pedestalG2 = time_start;

    experiment_settings = tmp_settings;
    calc_mean_pedestal(gain_pedestal.pedeG2, mean_pedestalG2);
    count_bad_pixel();
    log_pedestal_G2();
    sleep(1); // Just for safety
    return 0;
}

void reset_spot_statistics() {
    pthread_mutex_lock(&spots_statistics_mutex);

    // Per image statistics
    size_t omega_range = std::lround(experiment_settings.nimages_to_write * experiment_settings.omega_angle_per_image);
    spot_count_per_image.clear();
    spot_count_per_image.resize(omega_range, 0);
    spot_statistics_sequence = 0;

    // Resolution ring statistics
    // Not ideal, but resolution of edge with smaller d is selected.
    spot_statistics.resolution_limit = std::max((float)experiment_settings.spot_finding_resolution_limit,
            std::max(std::min(get_resolution_bottom(),get_resolution_top()),
                                                std::min(get_resolution_left(),get_resolution_right())));
    spot_statistics.resolution_bins = 20;

    spot_statistics.intensity.clear();
    spot_statistics.intensity.resize(spot_statistics.resolution_bins, 0);

    spot_statistics.mean_intensity.clear();
    spot_statistics.mean_intensity.resize(spot_statistics.resolution_bins, 0);

    spot_statistics.log_mean_intensity.clear();
    spot_statistics.log_mean_intensity.resize(spot_statistics.resolution_bins, 0);

    spot_statistics.count.clear();
    spot_statistics.count.resize(spot_statistics.resolution_bins, 0);

    spot_statistics.one_over_d2.clear();
    spot_statistics.mean_one_over_d2.clear();

    // Calculate 1/d^2 limits
    float one_over_dmin2 = 1/(spot_statistics.resolution_limit*spot_statistics.resolution_limit);

    for (int i = 0; i < spot_statistics.resolution_bins; i++) {
        float one_over_d2 = i * one_over_dmin2 / (float) spot_statistics.resolution_bins;
        spot_statistics.one_over_d2.push_back(one_over_d2);
    }
    // At the end, bound for last bin is provided
    spot_statistics.one_over_d2.push_back(one_over_dmin2);
    for (int i = 0; i < spot_statistics.resolution_bins; i++)
        spot_statistics.mean_one_over_d2.push_back((spot_statistics.one_over_d2[i+1]+spot_statistics.one_over_d2[i])/2.0);

    pthread_mutex_unlock(&spots_statistics_mutex);
}

// Arm is logic on measuring actual dataset
int jfwriter_arm() {
    time_t now;
    time(&now);

    writer_settings.timing_trigger = true;

    // Reset spots vector (purge spots found previously)
    spots.clear();
    // and also reset statistics
    reset_spot_statistics();

    // Master HDF5 file is only saved, when going through arm/disarm
    // This is explicitly to avoid writing master HDF5 file for pedestal
    if (writer_settings.HDF5_prefix != "")
        if (open_master_hdf5()) return 1;

    return jfwriter_start();
}

int jfwriter_disarm() {
    int err = jfwriter_stop();

    // Make log to InfluxDB
    if (!err) {
        if (experiment_settings.pedestalG0_frames > 0) {
            calc_mean_pedestal(gain_pedestal.pedeG0, mean_pedestalG0);
            count_bad_pixel();
            log_pedestal_G0();
        }
        log_measurement();
    }

    // Close master file - see comment above
    if (writer_settings.HDF5_prefix != "")
        if (close_master_hdf5()) return 1;

    return err;
}
