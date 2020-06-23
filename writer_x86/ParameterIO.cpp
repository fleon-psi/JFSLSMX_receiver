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

#define READOUT_TIME_IN_US 30

#define MIN_COUNT_TIME_IN_US 10
#define MAX_FRAME_TIME_FULL_SPEED_IN_US 450
#define MAX_FRAME_TIME_HALF_SPEED_IN_US 900

std::map<std::string, parameter_t> detector_options = {
        {"frame_time", {"s", PARAMETER_FLOAT, 0.0005, 2.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.frame_time; },
                               [](nlohmann::json &in) { experiment_settings.frame_time = in.get<double>(); update_summation(); },
                               "Time till start of next image"
                       }},
        {"count_time", {"s", PARAMETER_FLOAT, 0.0005, 1.0,true,
                               [](nlohmann::json &out) { out = experiment_settings.count_time; },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Counting time for one image"
                       }},
        {"frame_time_detector", {"us", PARAMETER_FLOAT, MAX_FRAME_TIME_FULL_SPEED_IN_US, 10000, false,
                               [](nlohmann::json &out) { out = experiment_settings.frame_time_detector * 1000000.0; },
                               [](nlohmann::json &in) {
                                   if (experiment_settings.frame_time_detector != in.get<double>() / 1000000.0) {
                                       experiment_settings.frame_time_detector = in.get<double>() / 1000000.0;
                                       time_pedestalG0.tv_sec = 0; // Frame time affects only G0 pedestal, which is marked as invalidated
                                       if (experiment_settings.frame_time_detector < experiment_settings.count_time_detector + READOUT_TIME_IN_US / 1e6) {
                                           experiment_settings.count_time_detector = experiment_settings.frame_time_detector - READOUT_TIME_IN_US / 1e6;
                                       }
                                       update_summation();
                                   }},
                               "Internal frame time of the detector"
                       }},
        {"count_time_detector", {"us", PARAMETER_FLOAT, MIN_COUNT_TIME_IN_US, 0.001, false,
                               [](nlohmann::json &out) { out = experiment_settings.count_time_detector * 1000000.0; },
                               [](nlohmann::json &in) {
                                   if (experiment_settings.count_time_detector != in.get<double>() / 1000000.0) {
                                       experiment_settings.count_time_detector = in.get<double>() / 1000000.0;
                                       time_pedestalG0.tv_sec = 0; // count time affects all pedestal, which is marked as invalidated
                                       time_pedestalG1.tv_sec = 0; // count time affects all pedestal, which is marked as invalidated
                                       time_pedestalG2.tv_sec = 0; // count time affects all pedestal, which is marked as invalidated
                                       if (experiment_settings.frame_time_detector < experiment_settings.count_time_detector + READOUT_TIME_IN_US / 1e6) {
                                           experiment_settings.frame_time_detector = experiment_settings.count_time_detector + READOUT_TIME_IN_US / 1e6;
                                       }
                                       update_summation();
                                   }
                               },
                               "Internal count time of the detector"
                       }},
        {"shutter_delay", {"ms", PARAMETER_FLOAT, 0.0, 1000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.shutter_delay * 1000.0; },
                               [](nlohmann::json &in) { experiment_settings.shutter_delay = in.get<double>() / 1000.0; update_summation(); },
                               "Time to open/close shutter"
                       }},
        {"beamline_delay", {"s", PARAMETER_FLOAT, 0.0, 100.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.beamline_delay; },
                               [](nlohmann::json &in) { experiment_settings.beamline_delay = in.get<double>(); update_summation(); },
                               "Maximal time taken by the beamline to prepare data collection, from the moment \"arm\" command is released"
                       }},
        {"pedestalG0_frames",{"", PARAMETER_UINT, 0, 10000, false,
                               [](nlohmann::json &out) { out = experiment_settings.pedestalG0_frames; },
                               [](nlohmann::json &in) {
                                   if (experiment_settings.pedestalG0_frames != in.get<uint32_t>()) {
                                       experiment_settings.pedestalG0_frames = in.get<uint32_t>();
                                       time_pedestalG0.tv_sec = 0;
                                       update_summation();
                                   }},
                               "Number of frames to collect for G0 pedestal"
                       }},
        {"pedestalG1_frames",{"", PARAMETER_UINT, 0, 10000, false,
                               [](nlohmann::json &out) { out = experiment_settings.pedestalG1_frames; },
                               [](nlohmann::json &in) {
                                   if (experiment_settings.pedestalG1_frames != in.get<uint32_t>()) {
                                       experiment_settings.pedestalG1_frames = in.get<uint32_t>();
                                       time_pedestalG1.tv_sec = 0;
                                   }},
                               "Number of frames to collect for G1 pedestal"
                       }},
        {"pedestalG2_frames",{"", PARAMETER_UINT, 0, 10000, false,
                               [](nlohmann::json &out) { out = experiment_settings.pedestalG2_frames; },
                               [](nlohmann::json &in) {
                                   if (experiment_settings.pedestalG2_frames != in.get<uint32_t>()) {
                                       experiment_settings.pedestalG2_frames = in.get<uint32_t>();
                                       time_pedestalG2.tv_sec = 0;
                                   }},
                               "Number of frames to collect for G1 pedestal"
                       }},
        {"summation",{"", PARAMETER_UINT, 1, 5000, true,
                               [](nlohmann::json &out) { out = experiment_settings.summation; },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Number of frames summed into a single image"
                       }},
        {"pixel_bit_depth",{"", PARAMETER_UINT, 1, 32, true,
                               [](nlohmann::json &out) { out = experiment_settings.pixel_depth * 8; },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Number of bits per image"
                       }},
        {"ntrigger",{"", PARAMETER_UINT, 1, 30000, false,
                               [](nlohmann::json &out) { out = experiment_settings.ntrigger; },
                               [](nlohmann::json &in) { experiment_settings.ntrigger = in.get<uint32_t>(); update_summation(); },
                               "Number of expected triggers"
                       }},
        {"frames_to_collect",{"", PARAMETER_UINT, 1, 30000, true,
                               [](nlohmann::json &out) { out = experiment_settings.ntrigger; },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Total number of frames collected by the detector (includes pedestal G0, beamline and shutter delays)"
                       }},
        {"total_images",{"", PARAMETER_UINT, 1, 1000000, true,
                               [](nlohmann::json &out) { out = experiment_settings.nimages_to_write; },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Total number of images expected to collect (ntrigger * nimages)"
                       }},
        {"nimages",{"", PARAMETER_UINT, 1, 1000000, false,
                               [](nlohmann::json &out) { out = experiment_settings.nimages_to_write_per_trigger; },
                               [](nlohmann::json &in) { experiment_settings.nimages_to_write_per_trigger = in.get<uint32_t>(); update_summation(); },
                               "Number of images per trigger"
                       }},
        {"photon_energy",{"keV", PARAMETER_FLOAT, 3.0, 30.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.energy_in_keV; },
                               [](nlohmann::json &in) { experiment_settings.energy_in_keV = in.get<double>(); },
                               "Expected energy of incoming photons"
                       }},
        {"wavelength",{"A", PARAMETER_FLOAT, WVL_1A_IN_KEV / 30.0, WVL_1A_IN_KEV / 3.0, false,
                               [](nlohmann::json &out) { out = WVL_1A_IN_KEV / experiment_settings.energy_in_keV; },
                               [](nlohmann::json &in) { experiment_settings.energy_in_keV = WVL_1A_IN_KEV / in.get<double>(); },
                                                            "Expected energy of incoming photons"
                       }},
        {"beam_center_x",{"pxl", PARAMETER_FLOAT, -3000.0, 3000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.beam_x; },
                               [](nlohmann::json &in) { experiment_settings.beam_x = in.get<double>(); },
                               "X beam center positions (in expanded detector coordinates)"
                       }},
        {"beam_center_y",{"pxl", PARAMETER_FLOAT, -3000.0, 3000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.beam_y; },
                               [](nlohmann::json &in) { experiment_settings.beam_y = in.get<double>(); },
                               "Y beam center positions (in expanded detector coordinates)"
                       }},
        {"detector_distance",{"mm", PARAMETER_FLOAT, 0.0, 5000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.detector_distance; },
                               [](nlohmann::json &in) { experiment_settings.detector_distance = in.get<double>(); },
                               "Y beam center positions (in expanded detector coordinates)"
                       }},
        {"total_flux",{"e/s", PARAMETER_FLOAT, 0.0, INFINITY, false,
                               [](nlohmann::json &out) { out = experiment_settings.total_flux; },
                               [](nlohmann::json &in) { experiment_settings.total_flux = in.get<double>(); },
                               "Total photon flux on the sample"
                       }},
        {"transmission",{"", PARAMETER_FLOAT, 0.0, 1.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.transmission; },
                               [](nlohmann::json &in) { experiment_settings.transmission = in.get<double>(); },
                               "Total photon flux on the sample"
                       }},
        {"beam_size_x",{"um", PARAMETER_FLOAT, 0.0, 5000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.beam_size_x; },
                               [](nlohmann::json &in) { experiment_settings.beam_size_x = in.get<double>(); },
                               "Beam size in horizontal direction"
                       }},
        {"beam_size_y",{"um", PARAMETER_FLOAT, 0.0, 5000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.beam_size_y; },
                               [](nlohmann::json &in) { experiment_settings.beam_size_y = in.get<double>(); },
                               "Beam size in vertical direction"
                       }},
        {"sample_temperature",{"K", PARAMETER_FLOAT, 0.0, 500.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.sample_temperature; },
                               [](nlohmann::json &in) { experiment_settings.sample_temperature = in.get<double>(); },
                               "Sample temperature"
                       }},
        {"spot_finding",{"", PARAMETER_BOOL, 0.0, 0.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.enable_spot_finding; },
                               [](nlohmann::json &in) {  experiment_settings.enable_spot_finding = in.get<bool>(); },
                               "Enable online spot finding algorithm"
                       }},
        {"spot_finding_strong_pixel",{"", PARAMETER_FLOAT, 1.0, 50.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.strong_pixel; },
                               [](nlohmann::json &in) {  experiment_settings.strong_pixel = in.get<double>(); },
                               "Strong pixel parameter for spot finding"
                       }},
        {"spot_finding_max_depth",{"", PARAMETER_UINT, 1.0, NIMAGES_PER_STREAM, false,
                               [](nlohmann::json &out) { out = experiment_settings.max_spot_depth; },
                               [](nlohmann::json &in) {  experiment_settings.max_spot_depth = in.get<uint16_t>(); },
                               "Spots present on more images than this value are discarded"
                       }},
        {"spot_finding_min_pixel",{"", PARAMETER_UINT, 1.0, 5000.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.min_pixels_per_spot; },
                               [](nlohmann::json &in) {  experiment_settings.min_pixels_per_spot = in.get<uint16_t>(); },
                               "Spots with less pixels than this value are discarded"
                       }},
        {"spot_finding_dimensions", {"", PARAMETER_STRING, 0.0, 0.0, false,
                               [](nlohmann::json &out) { experiment_settings.connect_spots_between_frames? out = "3D": out="2D";},
                               [](nlohmann::json &in) { if (in.get<std::string>() == "2D") experiment_settings.connect_spots_between_frames = false;
                               else if (in.get<std::string>() == "3D") experiment_settings.connect_spots_between_frames = true;
                               },
                               "Use 2D or 3D spot finding", {"2D","3D"}
                       }},
        // TODO: Check proper format of tracking_ID - discuss with Zac
        {"tracking_id",{"", PARAMETER_STRING, 0.0, 0.0, false,
                               [](nlohmann::json &out) { out = writer_settings.tracking_id;},
                               [](nlohmann::json &in) { writer_settings.tracking_id = in;},
                               "ActiveMQ tracking ID"
                       }},
        {"compression",{"", PARAMETER_STRING, 0.0, 0.0, false,
                               [](nlohmann::json &out) {
                                   if (writer_settings.compression == JF_COMPRESSION_NONE) out = "none";
                                   if (writer_settings.compression == JF_COMPRESSION_BSHUF_LZ4) out = "bslz4";
                                   if (writer_settings.compression == JF_COMPRESSION_BSHUF_ZSTD) out = "bszstd";},
                               [](nlohmann::json &in) {
                                   if (in.get<std::string>() == "none") writer_settings.compression = JF_COMPRESSION_NONE;
                                   if (in.get<std::string>() == "bslz4") writer_settings.compression = JF_COMPRESSION_BSHUF_LZ4;
                                   if (in.get<std::string>() == "bszstd") writer_settings.compression = JF_COMPRESSION_BSHUF_ZSTD;
                               },
                               "Compression algorithm", {"none", "bslz4", "bszstd"}
                       }},
        {"write_mode",{"", PARAMETER_STRING, 0.0, 0.0, false,
                               [](nlohmann::json &out) {
                                   if (writer_settings.write_mode == JF_WRITE_BINARY) out = "binary";
                                   if (writer_settings.write_mode == JF_WRITE_HDF5) out = "hdf5";
                                   if (writer_settings.write_mode == JF_WRITE_ZMQ) out = "zmq";
                               },
                               [](nlohmann::json &in) {
                                   if (in.get<std::string>() == "binary") writer_settings.write_mode = JF_WRITE_BINARY;
                                   if (in.get<std::string>() == "hdf5") writer_settings.write_mode = JF_WRITE_HDF5;
                                   // if (in.get<std::string>() == "zmq") writer_settings.write_mode = JF_WRITE_ZMQ;
                               },
                               "Mode of generating output", {"hdf5", "binary"}
                       }},
        // {"hdf5_version_compat",{}},
        {"name_pattern",{"", PARAMETER_STRING, 0.0, 0.0, false,
                               [](nlohmann::json &out) { out = writer_settings.HDF5_prefix; },
                               [](nlohmann::json &in) { writer_settings.HDF5_prefix = in.get<std::string>(); },
                               "Name pattern for output file"
                       }},
        {"omega_increment",{"deg", PARAMETER_FLOAT, 0.0, 20.0, false,
                               [](nlohmann::json &out) { out = experiment_settings.omega_angle_per_image; },
                               [](nlohmann::json &in) { experiment_settings.omega_angle_per_image = in.get<double>(); },
                               "Increase of omega angle per image (0 = raster)"
                       }},
        {"omega_start",{"deg", PARAMETER_FLOAT, -INFINITY, INFINITY, false,
                               [](nlohmann::json &out) { out = experiment_settings.omega_start; },
                               [](nlohmann::json &in) { experiment_settings.omega_start = in.get<double>(); },
                               "Start omega angle"
                       }},
        // TODO: This should be private variable - remove after development
        {"default_path",{"", PARAMETER_STRING, 0.0, 0.0, false,
                               [](nlohmann::json &out) { out = writer_settings.default_path; },
                               [](nlohmann::json &in) { writer_settings.default_path = in.get<std::string>(); },
                               "Path on the JF server to output files"
                       }},
        {"pedestalG0_mean", {"ADU", PARAMETER_FLOAT, 0.0, 0.0, true,
                               [](nlohmann::json &out) { for (int i = 0; i < NMODULES*NCARDS;i++) out.push_back(mean_pedestalG0[i]); },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Mean pedestal of gain G0 (per modules)"
                       }},
        {"pedestalG1_mean", {"ADU", PARAMETER_FLOAT, 0.0, 0.0, true,
                               [](nlohmann::json &out) { for (int i = 0; i < NMODULES*NCARDS;i++) out.push_back(mean_pedestalG1[i]); },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Mean pedestal of gain G1 (per module)"
                       }},
        {"pedestalG2_mean", {"ADU", PARAMETER_FLOAT, 0.0, 0.0, true,
                               [](nlohmann::json &out) { for (int i = 0; i < NMODULES*NCARDS;i++) out.push_back(mean_pedestalG2[i]); },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Mean pedestal of gain G2 (per module)"
                       }},
        {"bad_pixels", {"", PARAMETER_FLOAT, 0.0, 0.0, true,
                               [](nlohmann::json &out) { for (int i = 0; i < NMODULES*NCARDS;i++) out.push_back(bad_pixels[i]); },
                               [](nlohmann::json &in) { throw read_only_exception(); },
                               "Number of bad pixels (per module)"
                       }}


};

// Set initial parameters
void set_default_parameters() {
    experiment_settings.frame_time_detector = 500 / 1e6;
    experiment_settings.count_time_detector = 470 / 1e6;

    experiment_settings.frame_time = 500 / 1e6;
    experiment_settings.nimages_to_write = 1000;
    experiment_settings.ntrigger = 1;
    experiment_settings.energy_in_keV = 12.4;
    experiment_settings.pedestalG0_frames = 2000;
    experiment_settings.pedestalG1_frames = 1000;
    experiment_settings.pedestalG2_frames = 1000;
    experiment_settings.conversion_mode = MODE_CONV;
    experiment_settings.enable_spot_finding = true;
    experiment_settings.connect_spots_between_frames = true;
    experiment_settings.strong_pixel = 3.0;

    writer_settings.write_mode = JF_WRITE_HDF5;
    writer_settings.images_per_file = 1000;
    writer_settings.nthreads = NCARDS * 8; // Spawn 8 writer threads per card
    writer_settings.timing_trigger = true;
    writer_settings.hdf18_compat = false;
    writer_settings.default_path = "/mnt/ssd/";
    writer_settings.influxdb_url="http://mx-jungfrau-1:8086";

    //These parameters are not changeable at the moment
    writer_connection_settings[0].ib_dev_name = "mlx5_1";
    writer_connection_settings[0].receiver_host = "mx-ic922-1";
    writer_connection_settings[0].receiver_tcp_port = 52320;

    if (NCARDS == 2) {
        writer_connection_settings[1].ib_dev_name = "mlx5_12";
        writer_connection_settings[1].receiver_host = "mx-ic922-1";
        writer_connection_settings[1].receiver_tcp_port = 52321;
    }

    update_summation();
}

// Recalculates data collection parameters (summation, number of images, number of frames)
void update_summation() {

    if (experiment_settings.frame_time_detector < MAX_FRAME_TIME_HALF_SPEED_IN_US / 1e6)
        experiment_settings.jf_full_speed = true;
    else
        experiment_settings.jf_full_speed = false;

    experiment_settings.summation = std::lround(
            experiment_settings.frame_time / experiment_settings.frame_time_detector);

    // Summation
    if (experiment_settings.summation == 0) experiment_settings.summation = 1;

    experiment_settings.frame_time = experiment_settings.frame_time_detector * experiment_settings.summation;
    experiment_settings.count_time = experiment_settings.count_time_detector * experiment_settings.summation;

    if (experiment_settings.summation == 1) experiment_settings.pixel_depth = 2;
    else experiment_settings.pixel_depth = 4;

    if (experiment_settings.ntrigger == 0)
        experiment_settings.nimages_to_write = experiment_settings.nimages_to_write_per_trigger;
    else
        experiment_settings.nimages_to_write =
                experiment_settings.nimages_to_write_per_trigger * experiment_settings.ntrigger;

    experiment_settings.nframes_to_collect = experiment_settings.pedestalG0_frames
                                             + experiment_settings.summation * experiment_settings.nimages_to_write
                                             + experiment_settings.beamline_delay /
                                               experiment_settings.frame_time_detector
                                             + experiment_settings.shutter_delay * experiment_settings.ntrigger /
                                               experiment_settings.frame_time_detector;
}