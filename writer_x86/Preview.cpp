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

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <vector>

#include "JFWriter.h"

#define RED_ZERO UINT8_MAX
#define GREEN_ZERO UINT8_MAX
#define BLUE_ZERO UINT8_MAX

#define RED_MAX 0
#define GREEN_MAX 0
#define BLUE_MAX 0

int newest_preview_image() {
    for (int j = MAX_PREVIEW - 1;j >= 0; j-- ) {
        int count = 0;
        for (int i = 0; i < NCARDS; i ++) {
            if (preview_image_available[j*NCARDS+i]) count++;
        }
        if (count == NCARDS) return j;
    }
    return -1;
}

size_t expected_preview_images() {
    // Calculate how many preview images to generate
    size_t preview_stride = int(PREVIEW_FREQUENCY/experiment_settings.frame_time);

    // Stride need to be increased, if it would overflow preview buffer
    if ( MAX_PREVIEW < experiment_settings.nimages_to_write / preview_stride)
        preview_stride = experiment_settings.nimages_to_write / MAX_PREVIEW;

    return experiment_settings.nimages_to_write / preview_stride;
}

int update_jpeg_preview(std::vector<uchar> &jpeg_out, size_t image_number, float contrast) {
    cv::setNumThreads(0);

    cv::Mat values(YPIXEL, XPIXEL, CV_8U);
    
    // Color transformation
    for (int i = 0; i < YPIXEL; i++) {
        for (int j = 0; j < XPIXEL; j++) {
            float tmp = ((float) preview[image_number * PREVIEW_SIZE + i*XPIXEL+j]) / contrast;
            if (tmp >= 1.0) 
               values.at<uchar>(i,j) = 255;
            if (tmp <= 0.0)
               values.at<uchar>(i,j) = 0;
            else
               values.at<uchar>(i,j) = (uchar) std::lround(255.0 * tmp);
        }
    }


    cv::Mat image(YPIXEL, XPIXEL, CV_8UC3);
    cv::applyColorMap(values, image,  cv::COLORMAP_VIRIDIS);

    cv::imencode(".jpeg", image, jpeg_out);
    return 0;
}

int update_jpeg_preview_log(std::vector<uchar> &jpeg_out, size_t image_number, float contrast) {
    cv::setNumThreads(0);

    cv::Mat values(YPIXEL, XPIXEL, CV_8U);
    
    // Color transformation
    for (int i = 0; i < YPIXEL; i++) {
        for (int j = 0; j < XPIXEL; j++) {
            float tmp = preview[image_number * PREVIEW_SIZE + i*XPIXEL+j];
            if (tmp >= contrast) 
               values.at<uchar>(i,j) = 255;
            if (tmp < 1.0)
               values.at<uchar>(i,j) = 0;
            else
               values.at<uchar>(i,j) = (uchar) std::lround(255.0 * logf(tmp)/logf(contrast));
        }
    }

    cv::Mat image(YPIXEL, XPIXEL, CV_8UC3);
    cv::applyColorMap(values, image,  cv::COLORMAP_VIRIDIS);

    cv::imencode(".jpeg", image, jpeg_out); 
    return 0;
}
