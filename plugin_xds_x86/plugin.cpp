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

#include <unistd.h>
#include <iostream>
#include <string>
#include <pthread.h>
#include <hdf5.h>

#include "plugin.h"
#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"

// Taken from bshuf
extern "C" {
    uint32_t bshuf_read_uint32_BE(const void* buf);
    uint64_t bshuf_read_uint64_BE(const void* buf);
}

hid_t master_file_id;

int cache_nx;
int cache_ny;
int cache_nbytes;
int cache_nframes;
int cache_nframes_per_files;

uint32_t *mask;

pthread_mutex_t hdf5_mutex = PTHREAD_MUTEX_INITIALIZER;

int readInt(std::string location) {
    int data_out;
    hid_t dataset_id = H5Dopen2(master_file_id, location.c_str(), H5P_DEFAULT);
    hid_t dataspace = H5Dget_space(dataset_id);
    int ndims = H5Sget_simple_extent_ndims(dataspace);
    if (ndims != 1) return -1;

    hsize_t dims[1];

    H5Sget_simple_extent_dims(dataspace,dims, NULL);

    if (dims[0] != 1) return -1;

    herr_t status = H5Dread(dataset_id, H5T_NATIVE_INT,  H5S_ALL, H5S_ALL, H5P_DEFAULT, &data_out);

    H5Sclose(dataspace);
    H5Dclose(dataset_id);
    return data_out;
}

int readMask(std::string location) {
    pthread_mutex_lock(&hdf5_mutex);

    hid_t dataset_id = H5Dopen2(master_file_id, location.c_str(), H5P_DEFAULT);
    hid_t dataspace = H5Dget_space(dataset_id);
    
    int ndims = H5Sget_simple_extent_ndims(dataspace);

    if (ndims != 2) return 1;
    
    hsize_t dims[2];

    H5Sget_simple_extent_dims(dataspace,dims, NULL);

    if ((dims[0] != cache_ny) || (dims[1] != cache_nx)) return 1;

    herr_t status = H5Dread(dataset_id, H5T_NATIVE_INT,  H5S_ALL, H5S_ALL, H5P_DEFAULT, mask);
    H5Sclose(dataspace);
    H5Dclose(dataset_id);
    pthread_mutex_unlock(&hdf5_mutex);

    return 0;
}

void filter16(int16_t *in, int32_t *out) {
    for (int i = 0; i < cache_nx * cache_ny; i ++) {
        if ((in[i] < INT16_MIN+10) || (mask[i] != 0)) out[i] = -1;
        else if ((in[i] < 0) && (in[i] > INT16_MIN+10)) out[i] = 0;
        else if (in[i] > INT16_MAX-10) out[i] = INT32_MAX;
        else out[i] = in[i];
    }
}

void filter16(int32_t *in) {
    for (int i = 0; i < cache_nx * cache_ny; i ++) {
        if ((in[i] < INT16_MIN+10) || (mask[i] != 0)) in[i] = -1;
        else if ((in[i] < 0) && (in[i] > INT16_MIN+10)) in[i] = 0;
        else if (in[i] > INT16_MAX-10) in[i] = INT32_MAX;
    }
}

void filter32(int32_t *in) {
    for (int i = 0; i < cache_nx * cache_ny; i ++) {
        if ((in[i] < INT32_MIN+10) || (mask[i] != 0)) in[i] = -1;
        else if ((in[i] < 0) && (in[i] > INT32_MIN+10)) in[i] = 0;
    }
}

int read_frame(int frame_number, int32_t *output) {
    herr_t h5ret;

    int nfile = frame_number / cache_nframes_per_files;
    int nframe_in_file = frame_number % cache_nframes_per_files;
    
    char buff[255];
    snprintf(buff, 255, "/entry/data/data_%06d", nfile+1);

    pthread_mutex_lock(&hdf5_mutex);
    hid_t dataset_id = H5Dopen2(master_file_id, buff, H5P_DEFAULT);
    if (dataset_id < 0) return 1;

    hid_t dataspace_id = H5Dget_space(dataset_id);
    if (dataspace_id < 0) return 1;

    hsize_t dataset_dims[3];                            // dataset size
    hsize_t chunk_dims[3];                              // chunk size
    hsize_t hypers_dims[3] = {1, cache_ny, cache_nx}; // hyperslab size      
    hsize_t mem_dims[3]    = {1, cache_ny, cache_nx}; // memory space dimension
    hsize_t offset[3] = {nframe_in_file,0,0};    // hyperslab offset

    // Check size of dataset
    if (H5Sget_simple_extent_ndims(dataspace_id) != 3) return 1;
    H5Sget_simple_extent_dims(dataspace_id, dataset_dims, NULL);
    if (dataset_dims[0] <= nframe_in_file) return 1;
    if (dataset_dims[1] != cache_ny) return 1;
    if (dataset_dims[2] != cache_nx) return 1;

    // Get properties and see if filters are set
    hid_t dcpl_id = H5Dget_create_plist(dataset_id);
    int n_filters = H5Pget_nfilters(dcpl_id);

    if (n_filters == 0) {
        // Read using standard HDF5 API
    	// Setup hyperslab
        hid_t memspace_id = H5Screate_simple (3, mem_dims, NULL);     
        h5ret = H5Sselect_hyperslab(dataspace_id, H5S_SELECT_SET, offset, NULL, hypers_dims, NULL);

        // Read always to 32-bit int (this is what XDS likes)
        h5ret = H5Dread(dataset_id, H5T_NATIVE_INT32, memspace_id, dataspace_id, H5P_DEFAULT, output);
 
        pthread_mutex_unlock(&hdf5_mutex);

        // Filter bad pixels and mask based on bounds for particular data size
        if (cache_nbytes == 2)
            filter16(output);


        std::cout << output[847*1030*2+701] << std::endl;            
        std::cout << output[848*1030*2+701] << std::endl;            
        std::cout << output[847*1030*2+702] << std::endl;            
        std::cout << output[848*1030*2+702] << std::endl;            

        pthread_mutex_lock(&hdf5_mutex);
        H5Sclose(memspace_id);

    } else if (n_filters == 1) {
        // Check if chunk dimensions are OK
        H5Pget_chunk(dcpl_id, 3, chunk_dims);

        int y_ratio = dataset_dims[1] / chunk_dims[1];
        if (chunk_dims[2] != cache_nx) return 1;
        if (chunk_dims[0] != 1) return 1;
        if (y_ratio * chunk_dims[1] != dataset_dims[1]) return 1;

        // check if filter is bitshuffle
        unsigned int flags, filter_config, filter_mask;
        size_t cd_nelmts = 8;
        unsigned int cd_values[8];
        char name_buffer[255];

        h5ret = H5Pget_filter_by_id2(dcpl_id, BSHUF_H5FILTER, &flags, &cd_nelmts, cd_values, 255, name_buffer, &filter_config);

        if (cd_nelmts < 5) return 1;

        int16_t *decompressed_16;

        if (cache_nbytes == 2)
               decompressed_16 = (int16_t *) malloc(cache_nx * cache_ny * sizeof(int16_t));
        
        for (int i = 0; i < y_ratio; i++) {
            offset[1] += i * chunk_dims[1];

            // read raw dataset
            hsize_t raw_size;
            H5Dget_chunk_storage_size(dataset_id, offset, &raw_size);
            char *raw_chunk = (char *) malloc(raw_size);
            h5ret = H5Dread_chunk(dataset_id, H5P_DEFAULT, offset, &filter_mask, raw_chunk);

            int time = 0;
            while ((h5ret < 0) && (time < 30000)) {
                usleep(100);
                H5Drefresh(dataset_id);
                h5ret = H5Dread_chunk(dataset_id, H5P_DEFAULT, offset, &filter_mask, raw_chunk);
                time++;
            }
            pthread_mutex_unlock(&hdf5_mutex);

            // decompress & filter
            size_t decompressed_size = bshuf_read_uint64_BE(raw_chunk);
            size_t block_size = bshuf_read_uint32_BE(raw_chunk+8);

            if (decompressed_size != cache_nx*cache_ny*cache_nbytes/y_ratio) return 1;
            std::cout << decompressed_size << " " << i * cache_nx*cache_ny/y_ratio << " " << block_size << " " << raw_size << std::endl;

            if (cd_values[4] == BSHUF_H5_COMPRESS_ZSTD) {
                if (cache_nbytes == 2)
                   std::cout << bshuf_decompress_zstd(raw_chunk+12, decompressed_16 + i * cache_nx*cache_ny/y_ratio, cache_nx*cache_ny/y_ratio, 2, block_size) << std::endl;
                else
                   bshuf_decompress_zstd(raw_chunk+12, output + i * cache_nx*cache_ny/y_ratio, cache_nx*cache_ny/y_ratio, 4, block_size);
            } else if (cd_values[4] == BSHUF_H5_COMPRESS_LZ4) {
                if (cache_nbytes == 2)
                   std::cout << bshuf_decompress_lz4(raw_chunk+12, decompressed_16 + i * cache_nx*cache_ny/y_ratio, cache_nx*cache_ny/y_ratio, 2, block_size) << std::endl;
                else
                   bshuf_decompress_lz4(raw_chunk+12, output + i * cache_nx*cache_ny/y_ratio, cache_nx*cache_ny/y_ratio, 4, block_size);
            }

            free(raw_chunk);
            pthread_mutex_lock(&hdf5_mutex);
        }

        if (cache_nbytes == 2) {
           filter16(decompressed_16, output);
           free(decompressed_16);
        }
    }

    H5Pclose(dcpl_id);
    H5Sclose(dataspace_id);

    pthread_mutex_unlock(&hdf5_mutex);
    if (cache_nbytes == 4) filter32(output);

    return 0;
}

void printVersionInfo() {
   std::cout << "This is JUNGFRAU detector plugin (Copyright Dectris 2017 & Paul Scherrer Institute 2020)" << std::endl;
}

void setInfoArray(int info[1024])
{
    info[0] = CUSTOMER_ID;        // Customer ID [1:Dectris]
    info[1] = VERSION_MAJOR;      // Version  [Major]
    info[2] = VERSION_MINOR;      // Version  [Minor]
    info[3] = VERSION_PATCH;      // Version  [Patch]
    info[4] = VERSION_TIMESTAMP;  // Version  [timestamp]
}


void plugin_open(const char * filename,
                      int info_array[1024],
                      int * error_flag) {
    *error_flag = 0;

    setInfoArray(info_array);
    master_file_id = H5Fopen(filename, H5F_ACC_RDONLY | H5F_ACC_SWMR_READ, H5P_DEFAULT);

    if (master_file_id < 0) {
        std::cerr << "HDF5 error opening: " << filename << std::endl;
        *error_flag = -4;
    } else {
        // TODO: check this is JF4M
        cache_nx = readInt("/entry/instrument/detector/detectorSpecific/x_pixels_in_detector");
        cache_ny = readInt("/entry/instrument/detector/detectorSpecific/y_pixels_in_detector");
        cache_nbytes  = readInt("/entry/instrument/detector/bit_depth_image")/8;
        cache_nframes = readInt("/entry/instrument/detector/detectorSpecific/nimages") * readInt("/entry/instrument/detector/detectorSpecific/ntrigger");
        cache_nframes_per_files = readInt("/entry/instrument/detector/detectorSpecific/nimages_per_data_file");
        mask = (uint32_t *) malloc(cache_nx*cache_ny*sizeof(uint32_t));
        if (readMask("/entry/instrument/detector/pixel_mask") == 1) *error_flag = -4;
        std::cout << "File " << filename << " NX=" << cache_nx << " NY=" << cache_ny << " NBYTES=" << cache_nbytes << " IMAGES=" << cache_nframes << std::endl;
    }
}



void plugin_get_header(int *nx, int *ny,
                       int *nbytes,
                       float *qx, float *qy,
                       int * number_of_frames,
                       int info[1024],
                       int *error_flag)
{
    *nx = cache_nx;
    *ny = cache_ny;
    *nbytes = cache_nbytes;
    *qx = 0.075;
    *qy = 0.075;
    *number_of_frames = cache_nframes;
}

void plugin_get_data(int *frame_number,
                     int * nx, int * ny,
                     int data_array[],
                     int info_array[1024],
                     int *error_flag)
{
    setInfoArray(info_array);

    if(read_frame(*frame_number, data_array)) std::cerr << "Error reading frame " << *frame_number << std::endl;
}

void plugin_close(int *error_flag){
    herr_t status = H5Fclose(master_file_id);
}
