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

#include <iostream>
#include <cmath>
#include <algorithm>

#include <arpa/inet.h> // for ntohl
#include <unistd.h>    // for usleep

#include "JFWriter.h"


float calculate_wilson_b() {
    float sum_x = 0.0;
    float sum_y = 0.0;
    float sum_x2 = 0.0;
    float sum_xy = 0.0;

    for (int i = 0; i < spot_statistics.resolution_bins; i++) {
        sum_x += spot_statistics.mean_one_over_d2[i];
        sum_y += spot_statistics.log_mean_intensity[i];
        sum_x2 += spot_statistics.mean_one_over_d2[i]*spot_statistics.mean_one_over_d2[i];
        sum_xy += spot_statistics.mean_one_over_d2[i]*spot_statistics.log_mean_intensity[i];
    }
    float numerator = (spot_statistics.resolution_bins * sum_xy - sum_x * sum_y);
    float denominator =  (spot_statistics.resolution_bins * sum_x2 - sum_x * sum_x);
    // This is fitting ln<i> = a + b (1/d^2)
    // B = -b 2 in XDS terms
    if (denominator != 0) return - 2 * (numerator/ denominator);
        // Protect for denominator == 0
    else return -1;
}

void *run_metadata_thread(void* thread_arg) {
    // Read thread ID
    writer_thread_arg_t *arg = (writer_thread_arg_t *)thread_arg;
    int card_id   = arg->card_id;

    // This threads has exclusive access to sockfd - no mutex required
    exchange_magic_number(writer_connection_settings[card_id].sockfd);

    if (experiment_settings.conversion_mode == MODE_QUIT) {
        close(writer_connection_settings[card_id].sockfd);
        pthread_exit(0);

    }

    // NIMAGES_PER_STREAM is defined for 16-bit image, so it needs to be adjusted for 32-bit
    size_t images_per_stream = NIMAGES_PER_STREAM * 2 / experiment_settings.pixel_depth;

    // TODO: This function must be consistent with P9 receiver, so maybe should be moved to common location
    size_t total_chunks = experiment_settings.nimages_to_write / images_per_stream;
    // Account for leftover
    if (experiment_settings.nimages_to_write - total_chunks * images_per_stream > 0)
        total_chunks++;

    if (experiment_settings.enable_spot_finding) {
        size_t omega_range = std::lround(experiment_settings.nimages_to_write * experiment_settings.omega_angle_per_image);

        for (int chunk = 0; chunk < total_chunks; chunk++) {
            // Receive spots found by spot finder
            size_t spot_data_size;
            tcp_receive(writer_connection_settings[card_id].sockfd,(char *) &spot_data_size, sizeof(size_t));

            std::vector<spot_t> local_spots(spot_data_size);
            if (spot_data_size > 0)
                tcp_receive(writer_connection_settings[card_id].sockfd, (char *) local_spots.data(), spot_data_size * sizeof(spot_t));

            // Merge spots with the global list
            pthread_mutex_lock(&spots_mutex);
            spots.insert(spots.end(), local_spots.begin(), local_spots.end());
            pthread_mutex_unlock(&spots_mutex);

            // Update spots per frame statistics
            pthread_mutex_lock(&spots_statistics_mutex);
            for (int i = 0; i < local_spots.size() ; i++) {
                size_t omega = (size_t) std::lround(local_spots[i].z * experiment_settings.omega_angle_per_image);
                if ((omega >= 0) && (omega < omega_range))
                    spot_count_per_image[omega]++;

                if (local_spots[i].d > spot_statistics.resolution_limit) {
                    float one_over_d2 = 1 / (local_spots[i].d * local_spots[i].d);
                    int bin = int(spot_statistics.resolution_limit * spot_statistics.resolution_limit * one_over_d2 * spot_statistics.resolution_bins);

                    spot_statistics.intensity[bin] += local_spots[i].photons;
                    spot_statistics.count[bin] += 1;
                }
            }

            // Calculate Wilson plot
            // according to XDS CORRECT.LP
            // needs linear regression of ln(<i>) in function of (1/(4d^2))
            for (int i = 0; i < spot_statistics.resolution_bins; i++) {
                spot_statistics.mean_intensity[i] = spot_statistics.intensity[i]/ spot_statistics.count[i];
                spot_statistics.log_mean_intensity[i] = log(spot_statistics.mean_intensity[i]);
            }
            spot_statistics.wilson_B = calculate_wilson_b();

            spot_statistics_sequence++;
            pthread_mutex_unlock(&spots_statistics_mutex);
        }
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

    pthread_exit(0);
}
