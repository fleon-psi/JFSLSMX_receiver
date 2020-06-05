#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "JFWriter.h"

#define RED_ZERO UINT8_MAX
#define GREEN_ZERO UINT8_MAX
#define BLUE_ZERO UINT8_MAX

#define RED_MAX 0
#define GREEN_MAX 0
#define BLUE_MAX 0

int update_jpeg_preview() {
    int32_t overload;
    int32_t bad_pixel;
    int32_t preview_max = 0;

    if (experiment_settings.pixel_depth == 2) {overload= INT16_MAX - 10; bad_pixel = INT16_MIN + 10;}
    if (experiment_settings.pixel_depth == 4) {overload= INT32_MAX; bad_pixel = INT32_MIN;}

    for (int i = 0; i < YPIXEL * XPIXEL; i++) {
        if ((preview[i] > preview_max) && (preview[i] < overload)) preview_max = preview[i];
    }
    cv::Mat image(YPIXEL, XPIXEL, CV_16UC3);

    // Color transformation
    for (int i = 0; i < YPIXEL; i++) {
    for (int j = 0; j < XPIXEL; j++) {
        uint8_t r,g,b;
        if (preview[i] <= bad_pixel) {r = UINT8_MAX; g = 0; b = 0;} // Red
        else if (preview[i] >= overload) {r = 0; g = 0; b = UINT8_MAX;} // Blue
        else {
            float intensity = (float) preview[i] / (float) preview_max;
            if (intensity >= 0) {
               r = intensity * RED_MAX + (1-intensity) * RED_ZERO; 
               g = intensity * GREEN_MAX + (1-intensity) * GREEN_ZERO; 
               b = intensity * BLUE_MAX + (1-intensity) * BLUE_ZERO; 
            } else { r = RED_ZERO; g = GREEN_ZERO; b = BLUE_ZERO;}
        }
        // OpenCV uses BGR, not RGB
        image.at<cv::Vec3b>(i,j)[2] = r; image.at<cv::Vec3b>(i,j)[1] = g; image.at<cv::Vec3b>(i,j)[0] = b;
    }
    }
    std::vector<uchar> buf;
    cv::imencode("jpg", image, buf); 
    preview_jpeg = buf;
    return 0;
}
