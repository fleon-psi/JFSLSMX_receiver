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

#ifndef DIFFRACTIONEXPERIMENT_H
#define DIFFRACTIONEXPERIMENT_H

#include <chrono>
#include <exception>
#include <mutex>

#include "../json/single_include/nlohmann/json.hpp"
#include "Coord.h"

#define MAX_IMAGES              (10*1000*1000*1000L)
#define WVL_1A_IN_KEV           12.39854
#define READOUT_TIME_IN_US 30

#define MIN_COUNT_TIME_IN_US 10
#define MIN_FRAME_TIME_FULL_SPEED_IN_US 450

#define FRAME_TIME_PEDE_G1G2_IN_US (10*1000)

enum class CompressionAlgorithm {None, BSHUF_LZ4, BSHUF_ZSTD};
enum class DetectorMode         {Conversion, PedestalG0, PedestalG1, PedestalG2, Raw};
enum class WriterMode           {None, Binary, HDF5};

struct WrongParameterException : public std::exception
{
    std::string msg;
    WrongParameterException(const std::string& msg);
};

class DiffractionExperiment {
    mutable std::mutex m;

    DetectorMode  detector_mode{DetectorMode::Conversion};

    uint64_t images_per_trigger{0};
    uint64_t ntrigger{1};

    uint32_t pedestalG0_frames{2000};
    uint32_t pedestalG1_frames{2000};
    uint32_t pedestalG2_frames{2000};

    std::chrono::microseconds  frame_time_pedestalG1G2{10000}; // 100 Hz
    std::chrono::microseconds  frame_time{MIN_FRAME_TIME_FULL_SPEED_IN_US};
    std::chrono::microseconds  count_time{MIN_FRAME_TIME_FULL_SPEED_IN_US-READOUT_TIME_IN_US};
    std::chrono::microseconds  beamline_delay{0};        // delay between arm succeed and trigger opening (maximal)
    std::chrono::microseconds  shutter_delay{0};         // delay between trigger and shutter

    size_t summation{1};

    double   energy_in_keV{12.4};        // in keV
    double   beam_x{0};                  // in pixel
    double   beam_y{0};                  // in pixel
    double   detector_distance{100};     // in mm
    double   transmission{-1};           // 1.0 = full transmission
    double   total_flux{-1};             // in e/s
    double   omega_start{0};             // in degrees
    double   omega_angle_per_image{0};   // in degrees
    double   beam_size_x{-1};            // in micron
    double   beam_size_y{-1};            // in micron
    double   sample_temperature{-1};     // in K

    bool     enable_spot_finding{false};         // true = spot finding is ON
    bool     connect_spots_between_frames{true}; // true = rotation measurement, false = raster, jet
    double   spot_finding_resolution_limit{0.0}; // Resolution limit, above which spots are discarded
    double   strong_pixel{3};                    // STRONG_PIXEL in XDS
    uint32_t max_spot_depth{100};                // Maximum images per spot
    uint32_t min_pixels_per_spot{6};             // Minimum pixels per spot

    Coord   scattering_vector{0,0,1};  // S0 in Kabsch Acta D paper
    Coord   rotation_axis{1,0,0};      // m2 in Kabsch Acta D paper

    std::string file_prefix{"test"};    // Files are saved in default_path + "/" + HDF5_prefix + "_master.h5" and "_data.h5"
    std::string tracking_id{""};        // Dataset tracking ID, assigned by beamline
    uint32_t images_per_file{100};         // Images saved in a single file

    CompressionAlgorithm compression{CompressionAlgorithm::BSHUF_LZ4};  // Compression
    WriterMode writer_mode{WriterMode::HDF5};                           // Writing mode
    bool hdf18_compat{false};                                           // True = (compatibility with HDF5 1.8), False = (use SWMR and VDS)
public:
    DiffractionExperiment();
    DiffractionExperiment(const nlohmann::json &j);
    void Export(nlohmann::json &j) const;
    void Import(const nlohmann::json &j);

    void SetDetectorMode(DetectorMode input);
    void SetDetectorMode(const std::string &input);
    void SetImagesPerTrigger(uint64_t input);
    void SetNumTriggers(uint64_t triggers);

    void SetPedestalG0Frames(uint32_t input);
    void SetPedestalG1Frames(uint32_t input);
    void SetPedestalG2Frames(uint32_t input);
    void SetImageTime_us(uint64_t input);
    void SetShutterDelay(std::chrono::microseconds input);
    void SetBeamlineDelay(std::chrono::microseconds input);

    void SetPedestalG1G2FrameTime_us(uint64_t input);
    void SetEnergy_keV(double input);
    void SetWavelength(double input);
    void SetBeamX_pxl(double input);
    void SetBeamY_pxl(double input);
    void SetDetectorDistance_mm(double input);
    void SetTransmission(double input);
    void SetTotalFlux(double input);
    void SetOmegaStart(double input);
    void SetOmegaIncrement(double input);
    void SetBeamSizeX_um(double input);
    void SetBeamSizeY_um(double input);
    void SetSampleTemperature(double input);
    void EnableSpotFinding(bool input);
    void Enable3DSpotFinding(bool input);
    void SetStrongPixel(double input);
    void SetSpotFindingResolutionLimit(double input);
    void SetMaxSpotDepth(uint32_t input);
    void SetMinPixelsPerSpot(uint32_t input);
    void SetScatteringVector(const Coord &input);
    void SetRotationAxis(const Coord &input);
    void SetFilePrefix(const std::string &input);
    void SetTrackingID(const std::string &input);
    void SetImagesPerFile(uint32_t input);
    void SetCompression(CompressionAlgorithm input);
    void SetCompression(const std::string &input);
    void SetWriterMode(WriterMode input);
    void SetWriterMode(const std::string &input);
    void SetHDF5_18_Compat(bool input);

    DetectorMode GetDetectorMode() const;

    uint8_t GetPixelDepth() const;
    uint64_t GetNumTriggers() const;
    uint64_t GetImageCountPerTrigger() const;
    uint64_t GetImageCount() const;
    uint64_t GetPedestalG0Frames() const;
    uint64_t GetPedestalG1Frames() const;
    uint64_t GetPedestalG2Frames() const;

    uint64_t GetTotalFrames() const;
    std::chrono::microseconds GetFrameTime() const;
    std::chrono::microseconds GetImageTime() const;
    uint64_t GetSummation() const;
    std::chrono::microseconds GetImageCountTime() const;
    std::chrono::microseconds GetFrameCountTime() const;
    std::chrono::microseconds GetShutterDelay() const;
    std::chrono::microseconds GetBeamlineDelay() const;
    double GetEnergy_keV() const;
    double GetWavelength() const;
    double GetBeamX_pxl() const;
    double GetBeamY_pxl() const;
    double GetDetectorDistance_mm() const;
    double GetTransmission() const;
    double GetTotalFlux() const;
    double GetOmegaStart() const;
    double GetOmegaAngleIncrement() const;
    double GetOmega(double image) const;
    double GetBeamSizeX_um() const;
    double GetBeamSizeY_um() const;
    double GetSampleTemperature() const;

    bool IsSpotFindingEnabled() const;
    bool IsSpotFinding3D() const;
    double GetSpotFindingResolutionLimit() const;
    double GetStrongPixelValue() const;
    uint16_t GetMaxSpotDepth() const;
    uint16_t GetMinPixelsPerSpot() const;
    Coord GetRotationAxis() const;
    Coord GetScatteringVector() const;
    std::string GetTrackingID() const;
    std::string GetFilePrefix() const;
    int GetImagesPerFile() const;
    CompressionAlgorithm GetCompressionAlgorithm() const;
    WriterMode GetWriterMode() const;
    bool IsHDF18Compatible() const;
};

#endif //DIFFRACTIONEXPERIMENT_H
