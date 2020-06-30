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
#include <map>
#include "JFReceiver.h"
#include "../include/xray.h"

// CPU part of spot finding
// (recursion will not fit well to GPU)
// Constructing spot from strong pixels

// Adds two spot measurements
void merge_spots(spot_t &spot1, const spot_t spot2) {
    spot1.x = spot1.x + spot2.x;
    spot1.y = spot1.y + spot2.y;
    spot1.z = spot1.z + spot2.z;

    spot1.photons = spot1.photons + spot2.photons;

    if (spot2.min_line < spot1.min_line) spot1.min_line = spot2.min_line;
    if (spot2.min_col < spot1.min_col) spot1.min_col = spot2.min_col;

    if (spot2.max_line > spot1.max_line) spot1.max_line = spot2.max_line;
    if (spot2.max_col > spot1.max_col) spot1.max_col = spot2.max_col;

    if (spot2.first_frame < spot1.first_frame) spot1.first_frame = spot2.first_frame;
    if (spot2.last_frame > spot1.last_frame) spot1.last_frame = spot2.last_frame;
}

typedef std::pair<int16_t, int16_t> coordxy_t; // This is simply (x, y)
typedef std::map<coordxy_t, float> strong_pixel_map_t;
// This is mapping (x,y) --> intensity
// it allows to find if there is spot in (x,y) in log time
typedef std::vector<strong_pixel_map_t> strong_pixel_maps_t;
// There is one map per 1/2 frame

// Creates a continuous spot
// strong pixels are loaded into dictionary (one dictionary per frame)
// and routine checks if neighboring pixels are also in dictionary (likely in log(N) time)
// i is number of fragment (2 modules in horizontal direction)
// so assuming this is 2Mpixel, i/2 is frame number and i%2 defines position in vertical direction
spot_t add_pixel(strong_pixel_maps_t &strong_pixel_maps, size_t i, strong_pixel_map_t::iterator &it, bool connect_frames) {
    spot_t ret_value;

    float photons = it->second;
    int16_t col = it->first.first;
    int16_t line = it->first.second;

    strong_pixel_maps[i].erase(it); // Remove strong pixel from the dictionary, so it is not processed again

    ret_value.x = col * photons; // position is weighted by number of photon counts
    ret_value.y = (line + (i%2) * LINES) * photons;
    // Y has to be corrected for vertical position of the fragment
    ret_value.z = (i / 2) * photons;
    ret_value.photons = photons;

    ret_value.first_frame = i/2;
    ret_value.last_frame = i/2;
    ret_value.max_col = col;
    ret_value.min_col = col;
    ret_value.max_line = line;
    ret_value.min_line = line;

    strong_pixel_map_t::iterator it2;

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col-1, line  ))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col-1, line+1))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col-1, line-1))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col+1, line  ))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col+1, line-1))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col+1, line+1))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col  , line-1))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if ((it2 = strong_pixel_maps[i].find(coordxy_t(col  , line+1))) != strong_pixel_maps[i].end())
        merge_spots(ret_value, add_pixel(strong_pixel_maps, i, it2, connect_frames));

    if (connect_frames && (i + 2 < strong_pixel_maps.size())) {
        if ((it2 = strong_pixel_maps[i+2].find(coordxy_t(col  , line))) != strong_pixel_maps[i+2].end())
            merge_spots(ret_value, add_pixel(strong_pixel_maps, i+2, it2, connect_frames));
    }
    return ret_value;
}

void analyze_spots(strong_pixel *host_out, std::vector<spot_t> &spots, bool connect_frames, size_t images, size_t image0) {
    // key is location of strong pixel - value is number of photons
    // there is one mpa per fragment analyzed by GPU (2 horizontally connected modules)
    strong_pixel_maps_t strong_pixel_maps = strong_pixel_maps_t(images*2);

    float one_over_wavelength = experiment_settings.energy_in_keV /  WVL_1A_IN_KEV; // [A^-1]
    float omega_increment_in_radian = experiment_settings.omega_angle_per_image * M_PI / 180.0;

    // Transfer strong pixels into dictionary
    for (size_t i = 0; i < images*2; i++) {
        size_t addr = i * MAX_STRONG;
        int k = 0;
        // There is maximum MAX_STRONG pixels
        // GPU kernel sets col to -1 for next element after last strong pixel
        // Photons equal zero could mean that kernel was not at all executed
        while ((k < MAX_STRONG) && (host_out[addr + k].col >= 0) && (host_out[addr + k].line >= 0) && (host_out[addr+k].photons > 0)) {
            coordxy_t key = coordxy_t(host_out[addr + k].col, host_out[addr + k].line);
            if (bad_pixels.find(key) == bad_pixels.end())
//                  strong_pixel_maps[i][key] = host_out[addr + k].photons / ((2*NBX+1)*(2*NBY+1));
                strong_pixel_maps[i][key] = host_out[addr + k].photons;
            k++;
        }
    }

    for (int i = 0; i < images*2; i++) {
        strong_pixel_map_t::iterator iterator = strong_pixel_maps[i].begin();
        while (iterator != strong_pixel_maps[i].end()) {
            spot_t spot = add_pixel(strong_pixel_maps, i, iterator, connect_frames);

            // Spot has at least minimum number of pixels
            if (spot.last_frame - spot.first_frame + spot.max_col - spot.min_col + spot.max_line - spot.min_line + 1 > experiment_settings.min_pixels_per_spot)
                // Apply pixel count cut-off and cut-off of number of frames, which spot can span
                // (spots present in most frames, are likely to be either bad pixels or in spindle axis)
                spot.x = spot.x / spot.photons;
                // Account for the fact, that each process handles only part of the detector
                spot.y = spot.y / spot.photons + (NCARDS - receiver_settings.gpu_device - 1) * 2 * LINES;
                // Account for frame number
                spot.z = spot.z / spot.photons + image0;

                // Find lab coordinates of the pixel
                float lab[3];
                detector_to_lab(spot.x, spot.y, lab);

                // Get resolution
                spot.d = get_resolution(lab);

                // Check spot resolution
                if (spot.d > experiment_settings.spot_finding_resolution_limit) {
                    // Get reciprocal coordinates of spot
                    // For 3D spot-finding/indexing one needs to "rotate" reciprocal space vectors
                    // back to omega = 0
                    if (omega_increment_in_radian != 0.0) {
                        float p[3];
                        lab_to_reciprocal(p, lab, one_over_wavelength);
                        reciprocal_rotate(spot.q, p, omega_increment_in_radian * spot.z);
                    } else
                        lab_to_reciprocal(spot.q, lab, one_over_wavelength);

                    // Spot is put on the list
                    spots.push_back(spot);
                }
            }
            iterator = strong_pixel_maps[i].begin(); // Get first yet unprocessed spot in this frame
        }
    }
}
