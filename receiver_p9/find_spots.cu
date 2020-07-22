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

// Acknowledgements K. Diederichs (U. Konstanz)

#include <sys/types.h>
#include <sys/socket.h>

#include <iostream>
#include "JFReceiver.h"

// modules are stacked two vertically
// 67 (modules 6 and 7)
// 45
// 32
// 01
// --> but this part of app cares about four top/bottom modules
// --> so one chunk will be 67 and another 45 (or resp. 32 and 01)
#define FRAGMENT_SIZE_16 ((NMODULES/2) * COLS * LINES * sizeof(int16_t))

// CUDA calculation streams
cudaStream_t stream[NCUDA_STREAMS];

// GPU kernel to find strong pixels
template<typename T>
__global__ void find_spots_colspot(T *in, strong_pixel *out, float strong, int N) {
     if (blockIdx.x * blockDim.x + threadIdx.x < N) {
        // Threshold for signal^2 / var
        // To avoid division (see later) N/(N-1) factor is included already in the threshold
        float threshold = strong * strong * (float)((2*NBX+1) * (2*NBY+1)) / (float) ((2*NBX+1) * (2*NBY+1)-1);

        // One thread is 514 lines or 2 modules (in 2x2 configuration)
        // line0 points to the module/frame
        size_t line0 = (blockIdx.x * blockDim.x + threadIdx.x) * LINES;

        // Location of the first strong pixel in the output array 
        size_t strong_id0 = (blockIdx.x * blockDim.x + threadIdx.x) * MAX_STRONG;
        size_t strong_id = 0;

        // Sum and sum of squares of (2*NBY+1) vertical elements 
        // These are updated after each line is finished
        // 64-bit integer guarantees calculations are made without rounding errors
        int64_t sum_vert[COLS];
        int64_t sum2_vert[COLS];

        // Precalculate squares for first 2*NBY+1 lines
        for (int col = 0; col < COLS; col++) {
            int64_t tmp = in[(line0) * COLS + col];
            sum_vert[col]  = tmp;
            sum2_vert[col] = tmp*tmp;
        }
 
        for (size_t line = 1; line < 2*NBY+1; line++) {
            for (int col = 0; col < COLS; col++) {
                int64_t tmp = in[(line0 + line) * COLS + col];
                sum_vert[col]  += tmp;
                sum2_vert[col] += tmp*tmp;
            }
        }

        // do calculations for lines NBY to MODULE_LINES - NBY
        for (int16_t line = NBY; line < LINES - NBY; line++) {

            // sum and sum of squares for (2*NBX+1) x (2*NBY+1) elements
            int64_t sum  = sum_vert[0]; // Should be divided (float)((2*NBX+1) * (2*NBY+1));
            int64_t sum2 = sum2_vert[0];

            for (int i = 1; i < 2*NBX+1; i ++) {
                sum  += sum_vert[i];
                sum2 += sum2_vert[i];
            }

            for (int16_t col = NBX; col < COLS - NBX; col++) {
                // At all cost division and sqrt must be avoided
                // as performance penalty is significant (2x drop)
                // instead, constants ((2*NBX+1) * (2*NBY+1)) and ((2*NBX+1) * (2*NBY+1)-1)
                // are included in the threshold
                int64_t var = (2*NBX+1) * (2*NBY+1) * sum2 - (sum * sum); // This should be divided by ((2*NBX+1) * (2*NBY+1)-1)*((2*NBX+1) * (2*NBY+1))
                int64_t in_minus_mean = in[(line0 + line)*COLS+col] * ((2*NBX+1) * (2*NBY+1)) - sum; // Should be divided by ((2*NBX+1) * (2*NBY+1));

                if ((in_minus_mean > (2*NBX+1) * (2*NBY+1)) && // pixel value is larger than mean
                    (in[(line0 + line)*COLS+col] > 0) && // pixel is not bad pixel and is above 0
                    (in_minus_mean * in_minus_mean > var * threshold)) {
                       // Save line, column and photon count in output table
                       out[strong_id0+strong_id].line = line;
                       out[strong_id0+strong_id].col = col;
                       out[strong_id0+strong_id].photons = in_minus_mean;
                       strong_id = (strong_id + 1 ) % MAX_STRONG;
                    }

                // Updated value of sum and sum2
                // For last column - these need not to be calculated
                if (col < COLS - NBX - 1) {
                   sum += sum_vert[col + NBX + 1] - sum_vert[col - NBX];
                   sum2 += sum2_vert[col + NBX + 1] - sum2_vert[col - NBX];

                }
            }
            // Shift sum_vert and sum2_vert by one line
            if (line < LINES - NBY - 1) {
                for (int col = 0; col < COLS; col++) {
                    int64_t tmp_sum  = (int64_t)in[(line0+line+NBY+1) * COLS + col] + (int64_t)in[(line0 + line-NBY) * COLS + col];
                    int64_t tmp_diff = (int64_t)in[(line0+line+NBY+1) * COLS + col] - (int64_t)in[(line0 + line-NBY) * COLS + col];
                    sum_vert[col]  += tmp_diff;
                    sum2_vert[col] += tmp_sum * tmp_diff; // in[(line0+line+NBY+1) * MODULE_COLS + col]^2 - in[(line0 + line-NBY) * MODULE_COLS + col]^2
                }
            }
        }
        // Mark, where useful data and in output table
        out[strong_id0+strong_id].line = -1;
        out[strong_id0+strong_id].col = -1;
        out[strong_id0+strong_id].photons = strong_id;
   }
}

char *gpu_data;
strong_pixel *gpu_out;

int setup_gpu(int device) {
    // Set device
    cudaSetDevice(device);

    // Register image buffer as HW pinned (this is also registered by IB verbs)
    cudaError_t err = cudaHostRegister(ib_buffer, ib_buffer_size, cudaHostRegisterPortable);
    if (err != cudaSuccess) {
         std::cerr << "GPU: Register error " << cudaGetErrorString(err) << " addr " << ib_buffer << " size " << ib_buffer_size << std::endl;
         return 1;
    }

    // NIMAGES_PER_STREAM * FRAGMENT_SIZE is the same for 16 and 32-bit image
    // there is half images per stream, but twice in size
    // Initialize input memory on GPU
    size_t gpu_data_size = NCUDA_STREAMS * NIMAGES_PER_STREAM * FRAGMENT_SIZE_16;
    err = cudaMalloc((void **) &gpu_data, gpu_data_size);
    if (err != cudaSuccess) {
         std::cerr << "GPU: Mem alloc. error (data) " <<  gpu_data_size / 1024 / 1024 << std::endl;
         return 1;
    }

    // Initialize output memory as GPU/CPU unified memory
    err = cudaMallocManaged((void **) &gpu_out, NCUDA_STREAMS * NIMAGES_PER_STREAM * 2 * MAX_STRONG * sizeof(strong_pixel)); // frame is divided into 2 vertical slices
    if (err != cudaSuccess) {
         std::cerr << "GPU: Mem alloc. error (output)" << std::endl;
         return 1;
    }

    // Create computing streams
    for (int i = 0; i < NCUDA_STREAMS; i++) {
        err = cudaStreamCreate(&stream[i]);
        if (err != cudaSuccess) {
            std::cerr << "GPU: Stream create error" << std::endl;
            return 1;
        }
    }

    // Setup synchronization
    for (int i = 0; i < NCUDA_STREAMS*CUDA_TO_IB_BUFFER; i++) {
            pthread_mutex_init(cuda_stream_ready_mutex+i, NULL);
            pthread_cond_init(cuda_stream_ready_cond+i, NULL);
            pthread_mutex_init(writer_threads_done_mutex+i, NULL);
            pthread_cond_init(writer_threads_done_cond+i, NULL);
    }
    return 0;
}

int close_gpu() {
    cudaFree(gpu_out);
    cudaFree(gpu_data);
    cudaError_t err = cudaHostUnregister(ib_buffer);
    for (int i = 0; i < NCUDA_STREAMS; i++)
        err = cudaStreamDestroy(stream[i]);

    // Close synchronization
    for (int i = 0; i < NCUDA_STREAMS*CUDA_TO_IB_BUFFER; i++) {
            pthread_mutex_destroy(cuda_stream_ready_mutex+i);
            pthread_cond_destroy(cuda_stream_ready_cond+i);
            pthread_mutex_destroy(writer_threads_done_mutex+i);
            pthread_cond_destroy(writer_threads_done_cond+i);
    }

    return 0;
}

void *run_gpu_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    // GPU device is valid on per-thread basis, so every thread needs to set it
    cudaSetDevice(receiver_settings.gpu_device);

    // NIMAGES_PER_STREAM is defined for 16-bit image, so it needs to be adjusted for 32-bit
    size_t images_per_stream = NIMAGES_PER_STREAM * 2 / experiment_settings.pixel_depth;
    size_t fragment_size = ((NMODULES/2) * COLS * LINES * experiment_settings.pixel_depth);

    size_t total_chunks = experiment_settings.nimages_to_write / images_per_stream;
    // Account for leftover
    if (experiment_settings.nimages_to_write - total_chunks * images_per_stream > 0)
           total_chunks++;

    size_t thread_id = arg->ThreadID;

    cudaEvent_t event_mem_copied;
    cudaEventCreate (&event_mem_copied);

    for (size_t chunk = thread_id;
         chunk < total_chunks;
         chunk += NCUDA_STREAMS) {

         std::vector<spot_t> spots;

         size_t ib_slice = chunk % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);

         size_t images = experiment_settings.nimages_to_write - chunk * images_per_stream;
         if (images > images_per_stream) images = images_per_stream;

         pthread_mutex_lock(writer_threads_done_mutex+ib_slice);
         // Wait till everyone is done
         while (writer_threads_done[ib_slice] < receiver_settings.compression_threads)
             pthread_cond_wait(writer_threads_done_cond+ib_slice, 
                               writer_threads_done_mutex+ib_slice);
         // Restore full values and continue
         writer_threads_done[ib_slice] = 0;
         pthread_mutex_unlock(writer_threads_done_mutex+ib_slice);

         // Here all writting is done, but it is guarranteed not be overwritten

         // Copy frames to GPU memory
         cudaError_t err;
         err = cudaMemcpyAsync(gpu_data + thread_id * images_per_stream * fragment_size, 
               ib_buffer + ib_slice * images_per_stream * fragment_size,
               images * fragment_size,
               cudaMemcpyHostToDevice, stream[thread_id]);
         if (err != cudaSuccess) {
             std::cerr << "GPU: memory copy error for slice " << thread_id << "/" << ib_slice << "frames: " << images << "(" << cudaGetErrorString(err) << ")" << std::endl;
             pthread_exit(0);
         }

         cudaEventRecord (event_mem_copied, stream[thread_id]);

         // Start GPU kernel
         if (experiment_settings.pixel_depth == 2)
             find_spots_colspot<int16_t> <<<images_per_stream * 2 / 32, 32, 0, stream[thread_id]>>>
                 ((int16_t *) (gpu_data + thread_id * images_per_stream * fragment_size),
                  gpu_out + thread_id * images_per_stream * 2 * MAX_STRONG,
                  experiment_settings.strong_pixel, images * 2);
         else
             find_spots_colspot<int32_t> <<<images_per_stream * 2 / 32, 32, 0, stream[thread_id]>>>
                 ((int32_t *) (gpu_data + thread_id * images_per_stream * fragment_size),
                  gpu_out + thread_id * images_per_stream * 2 * MAX_STRONG,
                  experiment_settings.strong_pixel, images * 2);

         // After data are copied, one can release buffer
         err = cudaEventSynchronize(event_mem_copied);
         if (err != cudaSuccess) {
             std::cerr << "GPU: memory copy error" << std::endl;
             pthread_exit(0);
         }

         // Broadcast to everyone waiting, that buffer can be overwritten by next iteration
         pthread_mutex_lock(cuda_stream_ready_mutex+ib_slice);
         cuda_stream_ready[ib_slice] = chunk + NCUDA_STREAMS*CUDA_TO_IB_BUFFER;
         pthread_cond_broadcast(cuda_stream_ready_cond+ib_slice);
         pthread_mutex_unlock(cuda_stream_ready_mutex+ib_slice);

         // Ensure kernel has finished
         err = cudaStreamSynchronize(stream[thread_id]);
         if (err != cudaSuccess) {
             std::cerr << "GPU: execution error" << std::endl;
             pthread_exit(0);
         }

         // Analyze results to find spots
         // gpu_out is in unified memory and doesn't need to be explicitly copied to CPU
         analyze_spots(gpu_out + thread_id * images_per_stream * 2 * MAX_STRONG, spots, experiment_settings.connect_spots_between_frames, images, chunk * images_per_stream);

         // Send spots found by spot finder via TCP/IP
         pthread_mutex_lock(&accepted_socket_mutex);
         size_t spot_data_size = spots.size();
         send(accepted_socket, &spot_data_size, sizeof(size_t), 0);
         send(accepted_socket, spots.data(), spot_data_size * sizeof(spot_t), 0);
         pthread_mutex_unlock(&accepted_socket_mutex);
    }
    cudaEventDestroy (event_mem_copied);
    pthread_exit(0);
}

