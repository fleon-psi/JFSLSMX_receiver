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

#include "JFWriter.h"

pthread_t *writer = NULL;
writer_thread_arg_t *writer_thread_arg = NULL;

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

#ifndef OFFLINE
sls::Detector *det;
#endif

time_t time_pedestalG0 = 0;
time_t time_pedestalG1 = 0;
time_t time_pedestalG2 = 0;
time_t time_datacollection = 0;
