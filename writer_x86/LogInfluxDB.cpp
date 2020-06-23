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

#include <atomic>

#include <pistache/client.h>
#include <pistache/http.h>
#include <pistache/net.h>

Pistache::Http::Client influxdb_client;

void init_influxdb_client() {
    auto opts = Pistache::Http::Client::options().threads(1).maxConnectionsPerHost(8);
    influxdb_client.init(opts);
}

void close_influxdb_client() {
    influxdb_client.shutdown();
}

void send_to_influxdb(std::string database, std::string measurement, std::string content, time_t timestamp) {
    std::string url =  writer_settings.influxdb_url + "/api/v2/write?bucket=" + database + "&precision=s";
    std::string body = measurement + ",hostname=mx-jungfrau-1" + " " + content + " " + std::to_string(timestamp);
    auto resp = influxdb_client.post(url).body(body).send();
    resp.then([](Pistache::Http::Response response) {}
            , [&](std::exception_ptr exc) {
        std::cerr << "Problem with InfluxDB" << std::endl;
    });
}

void log_pedestal_G0() {
    std::string content = "";
    for (int i = 0; i < NMODULES*NCARDS; i++) {
        content += "mod" + std::to_string(i) + "=" + std::to_string(mean_pedestalG0[i]);
        if (i < NMODULES*NCARDS-1) content += ",";
    }
    send_to_influxdb("jungfrau","pedestalG0", content, time_pedestalG0.tv_sec);
}

void log_pedestal_G1() {
    std::string content = "";
    for (int i = 0; i < NMODULES*NCARDS; i++) {
        content += "mod" + std::to_string(i) + "=" + std::to_string(mean_pedestalG1[i]);
        if (i < NMODULES*NCARDS-1) content += ",";
    }
    send_to_influxdb("jungfrau","pedestalG1", content, time_pedestalG1.tv_sec);
}

void log_pedestal_G2() {
    std::string content = "";
    for (int i = 0; i < NMODULES*NCARDS; i++) {
        content += "mod" + std::to_string(i) + "=" + std::to_string(mean_pedestalG2[i]);
        if (i < NMODULES*NCARDS-1) content += ",";
    }
    send_to_influxdb("jungfrau","pedestalG2", content, time_pedestalG2.tv_sec);
}

void log_measurement() {
    int64_t packets_received = (int64_t) online_statistics[0].good_packets + (int64_t) online_statistics[1].good_packets;

    // If trigger mode is on, FPGA will only count packets in images to write
    int64_t packets_expected;
    if (experiment_settings.ntrigger > 0)
        packets_expected = (experiment_settings.nimages_to_write * experiment_settings.summation)
                * NCARDS * NMODULES * 128;
    else packets_expected = experiment_settings.nframes_to_collect * NCARDS * NMODULES * 128;

    int64_t packets_lost = packets_expected - packets_received;

    std::string content = "frames_to_collect=" + std::to_string(experiment_settings.nframes_to_collect) +
            ",images_to_write=" + std::to_string(experiment_settings.nimages_to_write) +
            ",frame_time_detector=" + std::to_string(experiment_settings.frame_time_detector) +
            ",frame_time=" + std::to_string(experiment_settings.frame_time) +
            ",compressed_size=" + std::to_string(total_compressed_size) +
            ",compression_ratio=" + std::to_string((double) (experiment_settings.nimages_to_write * NCARDS * NPIXEL * experiment_settings.pixel_depth)/ (double) total_compressed_size) +
            ",omega_range=" + std::to_string(experiment_settings.omega_angle_per_image *  experiment_settings.nimages_to_write) +
            ",spots=" + std::to_string(spots.size()) +
            ",duration=" + std::to_string((time_end.tv_sec - time_start.tv_sec)*1000.0 + (time_end.tv_nsec - time_start.tv_nsec)/1000.0) +
            ",lost_packets=" + std::to_string(packets_lost);
    send_to_influxdb("jungfrau", "data_collection", content, time_start.tv_sec);
}

void log_error(std::string category, std::string msg) {
    time_t now;
    time(&now);

    std::string content = "msg=\"" +msg + "\"";
    send_to_influxdb("jungfrau,category=" + category, "errors", content, now);
}