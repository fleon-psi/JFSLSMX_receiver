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

#include "../include/DiffractionExperiment.h"

WrongParameterException::WrongParameterException(const std::string &input) {
    msg = input;
}

void DiffractionExperiment::SetDetectorMode(DetectorMode input) {
    std::lock_guard<std::mutex> lg(m);
    detector_mode = input;
}

void DiffractionExperiment::SetImagesPerTrigger(uint64_t input) {
    std::lock_guard<std::mutex> lg(m);
    if (ntrigger * input > MAX_IMAGES) throw WrongParameterException("Image limit " + std::to_string(MAX_IMAGES) + " exceeded");
    images_per_trigger = input;
}

void DiffractionExperiment::SetNumTriggers(uint64_t input) {
    std::lock_guard<std::mutex> lg(m);
    if (images_per_trigger * input > MAX_IMAGES) throw WrongParameterException("Image limit " + std::to_string(MAX_IMAGES) + " exceeded");
    ntrigger = input;
}

void DiffractionExperiment::SetImageTime_us(uint64_t input) {
    std::lock_guard<std::mutex> lg(m);
    if (input < MIN_FRAME_TIME_FULL_SPEED_IN_US)
        throw WrongParameterException("Shortest frame time allowed is  " + std::to_string(MIN_FRAME_TIME_FULL_SPEED_IN_US) + " us");

    summation = input / MIN_FRAME_TIME_FULL_SPEED_IN_US;
    frame_time = std::chrono::microseconds(input / summation); // select frame time, so that image time is its multpile
    count_time = std::chrono::microseconds(input / summation - READOUT_TIME_IN_US); // adjust image time
}

void DiffractionExperiment::SetShutterDelay(std::chrono::microseconds input) {
    std::lock_guard<std::mutex> lg(m);
    shutter_delay = input;
}

void DiffractionExperiment::SetBeamlineDelay(std::chrono::microseconds input) {
    std::lock_guard<std::mutex> lg(m);
    beamline_delay = input;
}

void DiffractionExperiment::SetPedestalG1G2FrameTime_us(uint64_t input) {
    std::lock_guard<std::mutex> lg(m);
    frame_time_pedestalG1G2 = std::chrono::microseconds(input);
}

void DiffractionExperiment::SetPedestalG0Frames(uint32_t input) {
    std::lock_guard<std::mutex> lg(m);
    pedestalG0_frames = input;
}

void DiffractionExperiment::SetPedestalG1Frames(uint32_t input) {
    std::lock_guard<std::mutex> lg(m);
    pedestalG1_frames = input;
}

void DiffractionExperiment::SetPedestalG2Frames(uint32_t input) {
    std::lock_guard<std::mutex> lg(m);
    pedestalG2_frames = input;
}


void DiffractionExperiment::SetEnergy_keV(double input) {
    std::lock_guard<std::mutex> lg(m);
    energy_in_keV = input;
}

void DiffractionExperiment::SetWavelength(double input) {
    std::lock_guard<std::mutex> lg(m);
    energy_in_keV = WVL_1A_IN_KEV / input;
}

void DiffractionExperiment::SetBeamX_pxl(double input) {
    std::lock_guard<std::mutex> lg(m);
    beam_y = input;
}

void DiffractionExperiment::SetBeamY_pxl(double input) {
    std::lock_guard<std::mutex> lg(m);
    beam_x = input;
}

void DiffractionExperiment::SetDetectorDistance_mm(double input) {
    std::lock_guard<std::mutex> lg(m);
    detector_distance = input;
}

void DiffractionExperiment::SetTransmission(double input) {
    if (input > 1.0) throw WrongParameterException("Transmission is only allowed in range <= 1.0");
    std::lock_guard<std::mutex> lg(m);
    transmission = input;
}

void DiffractionExperiment::SetTotalFlux(double input) {
    std::lock_guard<std::mutex> lg(m);
    total_flux = input;
}

void DiffractionExperiment::SetOmegaStart(double input) {
    std::lock_guard<std::mutex> lg(m);
    omega_start = 0;
}

void DiffractionExperiment::SetOmegaIncrement(double input) {
    std::lock_guard<std::mutex> lg(m);
    omega_angle_per_image = input;
}

void DiffractionExperiment::SetBeamSizeX_um(double input) {
    std::lock_guard<std::mutex> lg(m);
    beam_size_x = input;
}

void DiffractionExperiment::SetBeamSizeY_um(double input) {
    std::lock_guard<std::mutex> lg(m);
    beam_size_y = input;
}

void DiffractionExperiment::SetSampleTemperature(double input) {
    std::lock_guard<std::mutex> lg(m);
    sample_temperature = input;
}

void DiffractionExperiment::EnableSpotFinding(bool input) {
    std::lock_guard<std::mutex> lg(m);
    enable_spot_finding = input;
}

void DiffractionExperiment::Enable3DSpotFinding(bool input) {
    std::lock_guard<std::mutex> lg(m);
    connect_spots_between_frames = input;
}

void DiffractionExperiment::SetStrongPixel(double input) {
    std::lock_guard<std::mutex> lg(m);
    strong_pixel = input;
}

void DiffractionExperiment::SetSpotFindingResolutionLimit(double input) {
    std::lock_guard<std::mutex> lg(m);
    spot_finding_resolution_limit = input;
}

void DiffractionExperiment::SetMaxSpotDepth(uint32_t input) {
    std::lock_guard<std::mutex> lg(m);
    max_spot_depth = input;
}

void DiffractionExperiment::SetMinPixelsPerSpot(uint32_t input) {
    std::lock_guard<std::mutex> lg(m);
    min_pixels_per_spot = input;

}
void DiffractionExperiment::SetScatteringVector(const Coord &input) {
    std::lock_guard<std::mutex> lg(m);
    scattering_vector = input.Normalize();

}
void DiffractionExperiment::SetRotationAxis(const Coord &input) {
    std::lock_guard<std::mutex> lg(m);
    rotation_axis = input.Normalize();
}

void DiffractionExperiment::SetFilePrefix(const std::string &input) {
    std::lock_guard<std::mutex> lg(m);
    file_prefix = input;
}

void DiffractionExperiment::SetTrackingID(const std::string &input) {
    std::lock_guard<std::mutex> lg(m);
    tracking_id = input;
}

void DiffractionExperiment::SetImagesPerFile(uint32_t input) {
    if (input == 0) throw WrongParameterException("Images per file cannot be zero");
    std::lock_guard<std::mutex> lg(m);
    images_per_file = input;
}

void DiffractionExperiment::SetCompression(CompressionAlgorithm input) {
    std::lock_guard<std::mutex> lg(m);
    compression = input;
}

void DiffractionExperiment::SetWriterMode(WriterMode input) {
    std::lock_guard<std::mutex> lg(m);
    writer_mode = input;
}

void DiffractionExperiment::SetHDF5_18_Compat(bool input) {
    std::lock_guard<std::mutex> lg(m);
    hdf18_compat = input;
}

uint64_t DiffractionExperiment::GetNumTriggers() const {
    std::lock_guard<std::mutex> lg(m);
    return ntrigger;
}

DetectorMode DiffractionExperiment::GetDetectorMode() const {
    std::lock_guard<std::mutex> lg(m);
    return detector_mode;
}

uint64_t DiffractionExperiment::GetImageCountPerTrigger() const {
    std::lock_guard<std::mutex> lg(m);
    return images_per_trigger;
}

uint64_t DiffractionExperiment::GetImageCount() const {
    std::lock_guard<std::mutex> lg(m);

    switch (detector_mode) {
        case DetectorMode::Conversion:
            return images_per_trigger * ntrigger;
        case DetectorMode::Raw:
            return images_per_trigger * ntrigger * summation;
        case DetectorMode::PedestalG0:
        case DetectorMode::PedestalG1:
        case DetectorMode::PedestalG2:
        default:
            // No frames saved for pedestal runs
            return 0;
    }
}

uint64_t DiffractionExperiment::GetPedestalG0Frames() const {
    std::lock_guard<std::mutex> lg(m);
    return pedestalG0_frames;
}

uint64_t DiffractionExperiment::GetPedestalG1Frames() const {
    std::lock_guard<std::mutex> lg(m);
    return pedestalG1_frames;
}

uint64_t DiffractionExperiment::GetPedestalG2Frames() const {
    std::lock_guard<std::mutex> lg(m);
    return pedestalG2_frames;
}

uint64_t DiffractionExperiment::GetTotalFrames() const {
    std::lock_guard<std::mutex> lg(m);
    std::chrono::microseconds preparation_time = beamline_delay + (shutter_delay * 2) * ntrigger;
    switch (detector_mode) {
        case DetectorMode::Conversion:
        case DetectorMode::Raw:
            return (preparation_time / frame_time + pedestalG0_frames + images_per_trigger * ntrigger * summation);
        case DetectorMode::PedestalG0:
            return pedestalG0_frames;
        case DetectorMode::PedestalG1:
            return pedestalG1_frames;
        case DetectorMode::PedestalG2:
            return pedestalG2_frames;
        default:
            return 0;
    }

}

std::chrono::microseconds DiffractionExperiment::GetFrameTime() const {
    std::lock_guard<std::mutex> lg(m);
    switch (detector_mode) {
        case DetectorMode::PedestalG1:
        case DetectorMode::PedestalG2:
            return frame_time_pedestalG1G2;
        case DetectorMode::Conversion:
        case DetectorMode::Raw:
        case DetectorMode::PedestalG0:
        default:
            return frame_time;
    }
}

std::chrono::microseconds DiffractionExperiment::GetImageTime() const {
    std::lock_guard<std::mutex> lg(m);
    switch (detector_mode) {
        case DetectorMode::PedestalG1:
        case DetectorMode::PedestalG2:
            return frame_time_pedestalG1G2;
        case DetectorMode::Conversion:
            return frame_time * summation;
        case DetectorMode::Raw:
        case DetectorMode::PedestalG0:
        default:
            // no summation
            return frame_time;
    }
}

uint64_t DiffractionExperiment::GetSummation() const {
    std::lock_guard<std::mutex> lg(m);
    switch (detector_mode) {
        case DetectorMode::Conversion:
            return summation;
        case DetectorMode::Raw:
        case DetectorMode::PedestalG0:
        case DetectorMode::PedestalG1:
        case DetectorMode::PedestalG2:
        default:
            return 1;
    }
}

std::chrono::microseconds DiffractionExperiment::GetImageCountTime() const {
    std::lock_guard<std::mutex> lg(m);
    return count_time * summation;
}

std::chrono::microseconds DiffractionExperiment::GetFrameCountTime() const {
    std::lock_guard<std::mutex> lg(m);
    return count_time;
}

std::chrono::microseconds DiffractionExperiment::GetShutterDelay() const {
    std::lock_guard<std::mutex> lg(m);
    return shutter_delay;
}

std::chrono::microseconds DiffractionExperiment::GetBeamlineDelay() const {
    std::lock_guard<std::mutex> lg(m);
    return beamline_delay;
}

double DiffractionExperiment::GetEnergy_keV() const {
    std::lock_guard<std::mutex> lg(m);
    return energy_in_keV;
}

double DiffractionExperiment::GetWavelength() const {
    std::lock_guard<std::mutex> lg(m);
    return WVL_1A_IN_KEV / energy_in_keV;
}

double DiffractionExperiment::GetBeamX_pxl() const {
    std::lock_guard<std::mutex> lg(m);
    return beam_x;
}

double DiffractionExperiment::GetBeamY_pxl() const {
    std::lock_guard<std::mutex> lg(m);
    return beam_y;
}

double DiffractionExperiment::GetDetectorDistance_mm() const {
    std::lock_guard<std::mutex> lg(m);
    return detector_distance;
}

double DiffractionExperiment::GetTransmission() const {
    std::lock_guard<std::mutex> lg(m);
    return transmission;
}

double DiffractionExperiment::GetTotalFlux() const {
    std::lock_guard<std::mutex> lg(m);
    return total_flux;
}

double DiffractionExperiment::GetOmegaStart() const {
    std::lock_guard<std::mutex> lg(m);
    return omega_start;
}

double DiffractionExperiment::GetOmegaAngleIncrement() const {
    std::lock_guard<std::mutex> lg(m);
    return omega_angle_per_image;
}

double DiffractionExperiment::GetOmega(double image) const {
    std::lock_guard<std::mutex> lg(m);
    return omega_start + omega_angle_per_image * image;
}

double DiffractionExperiment::GetBeamSizeX_um() const {
    std::lock_guard<std::mutex> lg(m);
    return beam_size_x;
}

double DiffractionExperiment::GetBeamSizeY_um() const {
    std::lock_guard<std::mutex> lg(m);
    return beam_size_y;
}

double DiffractionExperiment::GetSampleTemperature() const {
    std::lock_guard<std::mutex> lg(m);
    return sample_temperature;
}

bool DiffractionExperiment::IsSpotFindingEnabled() const {
    std::lock_guard<std::mutex> lg(m);
    return enable_spot_finding;
}

bool DiffractionExperiment::IsSpotFinding3D() const {
    std::lock_guard<std::mutex> lg(m);
    return connect_spots_between_frames;
}

double DiffractionExperiment::GetSpotFindingResolutionLimit() const {
    std::lock_guard<std::mutex> lg(m);
    return spot_finding_resolution_limit;
}

double DiffractionExperiment::GetStrongPixelValue() const {
    std::lock_guard<std::mutex> lg(m);
    return strong_pixel;
}

uint16_t DiffractionExperiment::GetMaxSpotDepth() const {
    std::lock_guard<std::mutex> lg(m);
    return max_spot_depth;
}

uint16_t DiffractionExperiment::GetMinPixelsPerSpot() const {
    std::lock_guard<std::mutex> lg(m);
    return min_pixels_per_spot;
}

Coord DiffractionExperiment::GetRotationAxis() const {
    std::lock_guard<std::mutex> lg(m);
    return rotation_axis;
}

Coord DiffractionExperiment::GetScatteringVector() const {
    std::lock_guard<std::mutex> lg(m);
    return scattering_vector * (energy_in_keV / WVL_1A_IN_KEV);
}

std::string DiffractionExperiment::GetTrackingID() const {
    std::lock_guard<std::mutex> lg(m);
    return tracking_id;
}

std::string DiffractionExperiment::GetFilePrefix() const {
    std::lock_guard<std::mutex> lg(m);
    return file_prefix;
}

int DiffractionExperiment::GetImagesPerFile() const {
    std::lock_guard<std::mutex> lg(m);
    return images_per_file;
}

CompressionAlgorithm DiffractionExperiment::GetCompressionAlgorithm() const {
    std::lock_guard<std::mutex> lg(m);
    return compression;
}

WriterMode DiffractionExperiment::GetWriterMode() const {
    std::lock_guard<std::mutex> lg(m);
    return writer_mode;
}

bool DiffractionExperiment::IsHDF18Compatible() const {
    std::lock_guard<std::mutex> lg(m);
    return hdf18_compat;
}

void DiffractionExperiment::SetCompression(const std::string &input) {
    std::lock_guard<std::mutex> lg(m);
    if (input == "none") compression = CompressionAlgorithm::None;
    else if (input == "bshuf_lz4") compression = CompressionAlgorithm::BSHUF_LZ4;
    else if (input == "bshuf_zstd") compression = CompressionAlgorithm::BSHUF_ZSTD;
    else throw WrongParameterException("Compression algorithm " + input + " not supported");
}

void DiffractionExperiment::SetWriterMode(const std::string &input) {
    std::lock_guard<std::mutex> lg(m);
    if (input == "none") writer_mode = WriterMode::None;
    else if (input == "hdf5") writer_mode = WriterMode::HDF5;
    else if (input == "binary") writer_mode = WriterMode::Binary;
    else throw WrongParameterException("Writer mode " + input + " not supported");
}

void DiffractionExperiment::SetDetectorMode(const std::string &input) {
    std::lock_guard<std::mutex> lg(m);
    if (input == "pedestalG0") detector_mode = DetectorMode::PedestalG0;
    else if (input == "pedestalG1") detector_mode = DetectorMode::PedestalG1;
    else if (input == "pedestalG2") detector_mode = DetectorMode::PedestalG2;
    else if (input == "raw") detector_mode = DetectorMode::Raw;
    else if (input == "conversion") detector_mode = DetectorMode::Conversion;
    else throw WrongParameterException("Operation mode " + input + " not supported");
}

uint8_t DiffractionExperiment::GetPixelDepth() const {
    if (summation == 1) return 2;
    else return 4;
}

DiffractionExperiment::DiffractionExperiment() = default;

DiffractionExperiment::DiffractionExperiment(const nlohmann::json &j) {
    Import(j);
}

void DiffractionExperiment::Import(const nlohmann::json &j) {
    for (auto &it_json: j.items()) {
        const auto &variable = it_json.key();
        const auto &value = it_json.value();

        if (variable == "images_per_trigger") SetImagesPerTrigger(value);
        else if (variable == "ntrigger") SetNumTriggers(value);
        else if (variable == "pedestalG0_frames") SetPedestalG0Frames(value);
        else if (variable == "pedestalG1_frames") SetPedestalG1Frames(value);
        else if (variable == "pedestalG2_frames") SetPedestalG2Frames(value);
        else if (variable == "frame_time_pedestalG1G1") SetPedestalG1G2FrameTime_us(value);
        else if (variable == "beamline_delay") SetBeamlineDelay(std::chrono::microseconds(value));
        else if (variable == "shutter_delay") SetShutterDelay(std::chrono::microseconds(value));
        else if (variable == "image_time") SetImageTime_us(value);
        else if (variable == "energy") SetEnergy_keV(value);
        else if (variable == "beam_x") SetBeamX_pxl(value);
        else if (variable == "beam_y") SetBeamY_pxl(value);
        else if (variable == "detector_distance") SetDetectorDistance_mm(value);
        else if (variable == "transmission") SetTransmission(value);
        else if (variable == "total_flux") SetTotalFlux(value);
        else if (variable == "omega_start") SetOmegaStart(value);
        else if (variable == "omega_increment") SetOmegaIncrement(value);
        else if (variable == "beam_size_x") SetBeamSizeX_um(value);
        else if (variable == "beam_size_y") SetBeamSizeY_um(value);
        else if (variable == "sample_temperature") SetSampleTemperature(value);
        else if (variable == "spot_finding_enable") EnableSpotFinding(value);
        else if (variable == "spot_finding_3D") Enable3DSpotFinding(value);
        else if (variable == "spot_finding_d_min") SetSpotFindingResolutionLimit(value);
        else if (variable == "spot_finding_strong_pixel") SetStrongPixel(value);
        else if (variable == "spot_finding_max_depth") SetMaxSpotDepth(value);
        else if (variable == "spot_finding_min_pixels") SetMinPixelsPerSpot(value);
        else if (variable == "scattering_vector") SetScatteringVector(Coord(value[0], value[1], value[2]));
        else if (variable == "rotation_axis") SetRotationAxis(Coord(value[0], value[1], value[2]));
        else if (variable == "prefix") SetFilePrefix(value);
        else if (variable == "tracking_id") SetTrackingID(value);
        else if (variable == "images_per_file") SetImagesPerFile(value);
        else if (variable == "hdf5_18_compat") SetHDF5_18_Compat(value);
        else if (variable == "mode") SetDetectorMode(value.get<std::string>());
        else if (variable == "compression") SetCompression(value.get<std::string>());
        else if (variable == "writer_mode") SetWriterMode(value.get<std::string>());
    }
}

void DiffractionExperiment::Export(nlohmann::json &j) const {
    std::lock_guard<std::mutex> lg(m);

    j["images_per_trigger"] = images_per_trigger;
    j["ntrigger"] = ntrigger;
    j["pedestalG0_frames"] = pedestalG0_frames;
    j["pedestalG1_frames"] = pedestalG1_frames;
    j["pedestalG2_frames"] = pedestalG2_frames;
    j["frame_time_pedestalG1G1"] = frame_time_pedestalG1G2.count();
    j["beamline_delay"] = beamline_delay.count();
    j["shutter_delay"] = shutter_delay.count();
    j["image_time"] = summation * frame_time.count();

    j["energy"] = energy_in_keV;
    j["beam_x"] = beam_x;
    j["beam_y"] = beam_y;
    j["detector_distance"] = detector_distance;
    j["transmission"] = transmission;
    j["total_flux"] = total_flux;
    j["omega_start"] = omega_start;
    j["omega_increment"] = omega_angle_per_image;
    j["beam_size_x"] = beam_size_x;
    j["beam_size_y"] = beam_size_y;
    j["sample_temperature"] = sample_temperature;

    j["spot_finding_enable"] = enable_spot_finding;
    j["spot_finding_3D"] = connect_spots_between_frames;
    j["spot_finding_d_min"] = spot_finding_resolution_limit;
    j["spot_finding_strong_pixel"] = strong_pixel;
    j["spot_finding_max_depth"] = max_spot_depth;
    j["spot_finding_min_pixels"] = min_pixels_per_spot;

    j["scattering_vector"] = {scattering_vector.x, scattering_vector.y, scattering_vector.z};
    j["rotation_axis"] = {rotation_axis.x, rotation_axis.y, rotation_axis.z};
    j["prefix"] = file_prefix;
    j["tracking_id"] = tracking_id;
    j["images_per_file"] = images_per_file;
    j["hdf5_18_compat"] = hdf18_compat;

    switch (detector_mode) {
        case DetectorMode::Raw:
            j["mode"] = "raw";
            break;
        case DetectorMode::Conversion:
            j["mode"] = "conversion";
            break;
        case DetectorMode::PedestalG0:
            j["mode"] = "pedestalG0";
            break;
        case DetectorMode::PedestalG1:
            j["mode"] = "pedestalG1";
            break;
        case DetectorMode::PedestalG2:
            j["mode"] = "pedestalG2";
            break;
    }

    switch (compression) {
        case CompressionAlgorithm::None:
            j["compression"] = "none";
            break;
        case CompressionAlgorithm::BSHUF_LZ4:
            j["compression"] = "bshuf_lz4";
            break;
        case CompressionAlgorithm::BSHUF_ZSTD:
            j["compression"] = "bshuf_zstd";
            break;
    }

    switch (writer_mode) {
        case WriterMode::None:
            j["writer_mode"] = "none";
            break;
        case WriterMode::Binary:
            j["writer_mode"] = "binary";
            break;
        case WriterMode::HDF5:
            j["writer_mode"] = "hdf5";
            break;
    }
}