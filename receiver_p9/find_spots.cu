#include <fstream>
#include <iostream>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <map>
#include <vector>
#include <pthread.h>
#include "JFReceiver.h"

// Maximum number of strong pixel in 2 veritcal modules
// if there are more pixels, these will be overwritten
// in ring buffer fashion
#define MAX_STRONG 16384L

// Size of bounding box for pixel
#define NBX 3
#define NBY 3

// TODO - this should be in common header
#define COLS (2*1030L)
#define LINES (514L)
#define NCARDS 2

// modules are stacked two vertically
// 67 (modules 6 and 7)
// 45
// 32
// 01
// --> but this part of app cares about four top/bottom modules
// --> so one chunk will be 67 and another 45 (or resp. 32 and 01)
#define FRAME_SIZE ((NMODULES/2) * COLS * LINES * sizeof(int16_t))

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
            sum_vert[col]  = in[(line0) * COLS + col];
            sum2_vert[col] = in[(line0) * COLS + col]*in[(line0) * COLS + col];
        }
 
        for (size_t line = 1; line < 2*NBY+1; line++) {
            for (int col = 0; col < COLS; col++) {
                sum_vert[col]  += in[(line0 + line) * COLS + col];
                sum2_vert[col] += in[(line0 + line) * COLS + col] * in[(line0 + line) * COLS + col];
            }
        }

        // do calculations for lines NBY to MODULE_LINES - NBY
        for (size_t line = NBY; line < LINES - NBY; line++) {

            // sum and sum of squares for (2*NBX+1) x (2*NBY+1) elements
            int64_t sum  = sum_vert[0];
            int64_t sum2 = sum2_vert[0];

            for (int i = 1; i < 2*NBX+1; i ++) {
                sum  += sum_vert[i];
                sum2 += sum2_vert[i];
            }

            for (int col = NBX; col < COLS - NBX; col++) {

                // At all cost division and sqrt must be avoided
                // as performance penalty is significant (2x drop)
                // instead, constants ((2*NBX+1) * (2*NBY+1)) and ((2*NBX+1) * (2*NBY+1)-1)
                // are included in the threshold
                float var = (2*NBX+1) * (2*NBY+1) * sum2 - (sum * sum); // This should be divided by (float) ((2*NBX+1) * (2*NBY+1)-1)

                float mean = sum; // Should be divided (float)((2*NBX+1) * (2*NBY+1));
                float in_minus_mean = in[(line0 + line)*COLS+col] * (float)((2*NBX+1) * (2*NBY+1)) - mean; // Should be divided (float)((2*NBX+1) * (2*NBY+1));

                if ((in_minus_mean > 0.0f) && // pixel value is larger than mean
                    (mean > 0.0f) &&          // mean is larger than zero (no bad pixels)
                    (in[(line0 + line)*COLS+col] > 0) && // pixel is not bad pixel and is above 0
                    (in_minus_mean * in_minus_mean > var * threshold)) {
                       // Save line, column and photon count in output table
                       out[strong_id0+strong_id].line = line;
                       out[strong_id0+strong_id].col = col;
                       out[strong_id0+strong_id].photons = in[(line0 + line)*COLS+col];
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

int16_t *gpu_data16;
int32_t *gpu_data32;
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

    // Initialize input memory on GPU
    size_t gpu_data16_size = NCUDA_STREAMS * NIMAGES_PER_STREAM * FRAME_SIZE;
    err = cudaMalloc((void **) &gpu_data16, gpu_data16_size);
    if (err != cudaSuccess) {
         std::cerr << "GPU: Mem alloc. error (data) " <<  gpu_data16_size / 1024 / 1024 << std::endl;
         return 1;
    }

    // Initialize output memory on GPU
    err = cudaMalloc((void **) &gpu_out, NCUDA_STREAMS * NIMAGES_PER_STREAM * 2 * MAX_STRONG * sizeof(strong_pixel)); // frame is divided into 2 vertical slices
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
    cudaFree(gpu_data16);
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

// CPU part
// (recursion will not fit well to GPU)
// Constructing spot from strong pixels

// Adds two spot measurements
void merge_spots(spot_t &spot1, const spot_t spot2) {
        spot1.x = spot1.x + spot2.x;
        spot1.y = spot1.y + spot2.y;
        spot1.z = spot1.z + spot2.z;
        spot1.photons = spot1.photons + spot2.photons;
        spot1.pixels = spot1.pixels + spot2.pixels;
}

// If spots come from two different frames, depth needs to be incremented
void merge_spots_new_frame(spot_t &spot1, const spot_t spot2) {
        spot1.x = spot1.x + spot2.x;
        spot1.y = spot1.y + spot2.y;
        spot1.z = spot1.z + spot2.z;
        spot1.photons = spot1.photons + spot2.photons;
        spot1.pixels = spot1.pixels + spot2.pixels;
        spot1.depth = spot1.depth + spot2.depth + 1;
}

typedef std::pair<int16_t, int16_t> coordxy_t; // This is simply (x, y)
typedef std::map<coordxy_t, uint64_t> strong_pixel_map_t;
// This is mapping (x,y) --> intensity
// it allows to find if there is spot in (x,y) in log time
typedef std::vector<strong_pixel_map_t> strong_pixel_maps_t;
// There is one map per 1/2 frame

// Creates a continous spot
// strong pixels are loaded into dictionary (one dictionary per frame)
// and routine checks if neighboring pixels are also in dictionary (likely in log(N) time)
spot_t add_pixel(strong_pixel_maps_t &strong_pixel_maps, size_t i, strong_pixel_map_t::iterator &it, bool connect_frames) {
    spot_t ret_value;

    uint64_t photons = it->second;
    int16_t col = it->first.first;
    int16_t line = it->first.second;

    strong_pixel_maps[i].erase(it); // Remove strong pixel from the dictionary, so it is not processed again

    ret_value.x = col * (double)photons; // position is weighted by number of photon counts
    ret_value.y = (line + (i%2) * LINES) * (double)photons;
    // Y accounts for the actual module
    ret_value.z = (i / 2) * (double)photons;
    ret_value.photons = photons;
    ret_value.pixels = 1;
    ret_value.depth = 0;

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
            merge_spots_new_frame(ret_value, add_pixel(strong_pixel_maps, i+2, it2, connect_frames));
    }
    return ret_value;
}

void analyze_spots(strong_pixel *host_out, std::vector<spot_t> &spots, bool connect_frames, size_t images, size_t image0) {
    // key is location of strong pixel - value is number of photons
    // there is one mpa per fragment analyzed by GPU (2 horizontally connected modules)
    strong_pixel_maps_t strong_pixel_maps = strong_pixel_maps_t(images*2); 

    // Transfer strong pixels into dictionary
    for (size_t i = 0; i < images*2; i++) {
        size_t addr = i * MAX_STRONG;
        int k = 0;
        // There is maximum MAX_STRONG pixels
        // GPU kernel sets col to -1 for next element after last strong pixel
        // Photons equal zero could mean that kernel was not at all executed
        while ((k < MAX_STRONG) && (host_out[addr + k].col >= 0) && (host_out[addr + k].line >= 0) && (host_out[addr+k].photons > 0)) {
              coordxy_t key = coordxy_t(host_out[addr + k].col, host_out[addr + k].line);
              strong_pixel_maps[i][key] = host_out[addr + k].photons;
              k++;
        }
    }

    for (int i = 0; i < images*2; i++) {
      strong_pixel_map_t::iterator iterator = strong_pixel_maps[i].begin();
      while (iterator != strong_pixel_maps[i].end()) {
          spot_t spot = add_pixel(strong_pixel_maps, i, iterator, connect_frames);
          // Apply pixel count cut-off and cut-off of number of frames, which spot can span 
          // (spots present in most frames, are likely to be either bad pixels or in spindle axis)
          spot.x = spot.x / spot.photons;
          // Account for the fact, that each process handles only part of the detector
          spot.y = spot.y / spot.photons + (NCARDS - receiver_settings.card_number - 1) * 2 * LINES;
          // Account for frame number
          spot.z = spot.z / spot.photons + image0;
//          spots.push_back(spot);
          if ((spot.pixels > 3) && (spot.depth < 100)) spots.push_back(spot);
          iterator = strong_pixel_maps[i].begin(); // Get first unprocessed spot in this frame
      }
    }
}

void *run_gpu_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    // GPU device is valid on per-thread basis, so every thread needs to set it
    cudaSetDevice(receiver_settings.gpu_device);

    std::vector<spot_t> spots;

    // Account for leftover
    size_t total_chunks = experiment_settings.nimages_to_write / NIMAGES_PER_STREAM;
    if (experiment_settings.nimages_to_write - total_chunks * NIMAGES_PER_STREAM > 0)
           total_chunks++;

    size_t gpu_slice = arg->ThreadID;

    cudaEvent_t event_mem_copied;
    cudaEventCreate (&event_mem_copied);

    strong_pixel *host_out = (strong_pixel *) calloc(NIMAGES_PER_STREAM * 2 * MAX_STRONG, sizeof(strong_pixel));

    for (size_t chunk = gpu_slice;
         chunk < total_chunks;
         chunk += NCUDA_STREAMS) {

         size_t ib_slice = chunk % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);

//         size_t frame0 = ib_slice * NIMAGES_PER_STREAM;
         size_t images = experiment_settings.nimages_to_write - gpu_slice * NIMAGES_PER_STREAM;
         if (images > NIMAGES_PER_STREAM) images = NIMAGES_PER_STREAM;

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
         err = cudaMemcpyAsync(gpu_data16 + (gpu_slice % NCUDA_STREAMS) * NIMAGES_PER_STREAM * FRAME_SIZE / sizeof(uint16_t), 
               ib_buffer + ib_slice * NIMAGES_PER_STREAM * FRAME_SIZE,
               images * FRAME_SIZE,
               cudaMemcpyHostToDevice, stream[gpu_slice]);
         if (err != cudaSuccess) {
             std::cerr << "GPU: memory copy error for slice " << gpu_slice << "/" << ib_slice << "frames: " << images << "(" << cudaGetErrorString(err) << ")" << std::endl;
             pthread_exit(0);
         }

         cudaEventRecord (event_mem_copied, stream[gpu_slice]);

         // Start GPU kernel
         // TODO - handle frame summation
         find_spots_colspot<int16_t> <<<NIMAGES_PER_STREAM * 2 / 32, 32, 0, stream[gpu_slice]>>> 
                 (gpu_data16 + gpu_slice * NIMAGES_PER_STREAM * FRAME_SIZE / 2, 
                  gpu_out + gpu_slice * NIMAGES_PER_STREAM * 2 * MAX_STRONG, 
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
         err = cudaStreamSynchronize(stream[gpu_slice]);
         if (err != cudaSuccess) {
             std::cerr << "GPU: execution error" << std::endl;
             pthread_exit(0);
         }

         // Copy result back to host memory
         err = cudaMemcpy(host_out, 
                         gpu_out + gpu_slice * NIMAGES_PER_STREAM * 2 * MAX_STRONG,
                         images * 2 * MAX_STRONG * sizeof(strong_pixel),
                         cudaMemcpyDeviceToHost);

         // Analyze results to find spots
         analyze_spots(host_out, spots, experiment_settings.connect_spots_between_frames, images, chunk * NIMAGES_PER_STREAM);
    }
    cudaEventDestroy (event_mem_copied);

    // Merge calculated spots to a single vector
    pthread_mutex_lock(&all_spots_mutex);
    for (int i = 0; i < spots.size(); i++)
        all_spots.push_back(spots[i]);
    pthread_mutex_unlock(&all_spots_mutex);

//    std::cout << "GPU: Thread "<< arg->ThreadID << " done" << std::endl;
    pthread_exit(0);
}

