// Structure of the plugin comes from K. Diederichs (U. Konstanz)
// Implementation based on Neggia (Dectris) and Durin (Diamond Light Source)

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


#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdint.h>
#include "../include/JFApp.h"
#include "../writer_x86/JFWriter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CUSTOMER_ID        1  // Customer ID [1]
#define VERSION_MAJOR      0  // Version  [Major]
#define VERSION_MINOR      1  // Version  [Minor]
#define VERSION_PATCH      0  // Version  [Patch]
#define VERSION_TIMESTAMP -1  // Version  [timestamp]


void  plugin_open  (const char *,
                         int info_array[1024],
                         int *error_flag);

void plugin_get_header ( int * nx, int * ny,
                         int * nbytes,
                         float * qx, float * qy,
                         int * number_of_frames,
                         int info[1024],
                         int *error_flag);

void plugin_get_data   ( int *frame_number,
                         int * nx, int * ny,
                         int data_array[],
                         int info_array[1024],
                         int *error_flag);

void plugin_close (int *error_flag);

#ifdef  __cplusplus
}
#endif

#endif // PLUGIN_H
