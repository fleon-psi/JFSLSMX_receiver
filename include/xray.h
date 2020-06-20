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

#ifndef _XRAY_H
#define _XRAY_H

// Lab is in mm
// TODO: gaps and pixel size need to be declared in common header!
// TODO: Ensure signs are OK
inline void detector_to_lab(float x, float y, float lab[3]) {
    float x_with_gaps = x + int(x/1030) * 9;
    float y_with_gaps = y + int(y/514) * 36;
    float x_beam_coord = x_with_gaps - experiment_settings.beam_x;
    float y_beam_coord = y_with_gaps - experiment_settings.beam_y;
    lab[0] = x_beam_coord * 0.075;
    lab[1] = y_beam_coord * 0.075;
    lab[2] = experiment_settings.detector_distance;
}

inline float get_resolution(float lab[3]) {
    float wavelength = 12.398 / (experiment_settings.energy_in_keV);
    float beam_path = sqrt(lab[0]*lab[0] + lab[1]*lab[1] + lab[2]*lab[2]); 
    // Assumes planar detector, 90 deg towards beam
    float cos_2theta = beam_path / lab[2];
    // cos(2theta) = cos(theta)^2 - sin(theta)^2
    // cos(2theta) = 1 - 2*sin(theta)^2
    // Techinically two solutions for two theta, but it makes sense only to take positive one in this case
    float sin_theta = sqrt((1-cos_2theta)/2);
    return wavelength / (2*sin_theta);
}

#endif
