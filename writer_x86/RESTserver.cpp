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

#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/client.h>
#include <iostream>
#include <map>
#include <cmath>



#include "JFWriter.h"

#define API_VERSION "jf-0.1.0"
#define KEV_OVER_ANGSTROM 12.398

#define FRAME_TIME_FULL_SPEED 0.0005
#define COUNT_TIME_FULL_SPEED 0.00047

#define FRAME_TIME_HALF_SPEED 0.001
#define COUNT_TIME_HALF_SPEED 0.00001

#define PISTACHE_THREADS 4
#define PISTACHE_PORT 5232

pthread_mutex_t daq_state_mutex = PTHREAD_MUTEX_INITIALIZER;
enum daq_state_t {
    STATE_READY, STATE_ACQUIRE, STATE_ERROR, STATE_CHANGE, STATE_NOT_INITIALIZED
} daq_state;

std::string state_to_string() {
    switch (daq_state) {
        case STATE_READY:
            return "Ready";
        case STATE_ACQUIRE:
            return "Acquiring";
        case STATE_ERROR:
            return "Error";
        case STATE_CHANGE:
            return "In progress";
        case STATE_NOT_INITIALIZED:
            return "Not initialized";
        default:
            return "";
    }
}

// Recalculates data collection parameters (summation, number of images, number of frames)
// TODO: Should check count_time_detector is reasonable
void update_summation() {
    if (experiment_settings.jf_full_speed) {
        if (experiment_settings.frame_time_detector < FRAME_TIME_FULL_SPEED)
            experiment_settings.frame_time_detector = FRAME_TIME_FULL_SPEED;
//        if (experiment_settings.count_time_detector < COUNT_TIME_FULL_SPEED) experiment_settings.count_time_detector = COUNT_TIME_FULL_SPEED;
    } else {
        if (experiment_settings.frame_time_detector < FRAME_TIME_HALF_SPEED)
            experiment_settings.frame_time_detector = FRAME_TIME_HALF_SPEED;
//        if (experiment_settings.count_time_detector < COUNT_TIME_HALF_SPEED) experiment_settings.count_time_detector = COUNT_TIME_HALF_SPEED;
    }

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

// Set initial parameters
void default_parameters() {
    experiment_settings.jf_full_speed = false;
    experiment_settings.frame_time_detector = FRAME_TIME_HALF_SPEED;
    experiment_settings.count_time_detector = COUNT_TIME_HALF_SPEED;
    experiment_settings.summation = 1;
    experiment_settings.frame_time = FRAME_TIME_HALF_SPEED;
    experiment_settings.nimages_to_write = 0;
    experiment_settings.ntrigger = 0;
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

// Actions to execute by the detector
// TODO: Wrong parameters should raise errors
// TODO: Offer fast measurement mode for test shots - no pedestal G0, just external trigger
void detector_command(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    // For REST calls to be made from browser, two things have to be made:
    //   - all requests need origin-allow in http header
    //   - PUT requests require preflight authorization via OPTIONS request (see below)
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    // State is locked, so one cannot run two PUT commands at the same time
    pthread_mutex_lock(&daq_state_mutex);

    auto command = request.param(":command").as<std::string>();

    if (command == "initialize") {
        if (daq_state == STATE_ACQUIRE)
            jfwriter_disarm();
        daq_state = STATE_CHANGE;
        default_parameters();
        if (jfwriter_pedestalG0()) daq_state = STATE_ERROR;
        else if (jfwriter_pedestalG1()) daq_state = STATE_ERROR;
        else if (jfwriter_pedestalG2()) daq_state = STATE_ERROR;
        else daq_state = STATE_READY;
    } else if ((daq_state == STATE_READY) && (command == "arm")) {
        daq_state = STATE_CHANGE;
        if (jfwriter_arm()) daq_state = STATE_ERROR;
        else daq_state = STATE_ACQUIRE;
    } else if ((daq_state == STATE_READY) && (command == "pedestal")) {
        if (jfwriter_pedestalG0()) daq_state = STATE_ERROR;
        else if (jfwriter_pedestalG1()) daq_state = STATE_ERROR;
        else if (jfwriter_pedestalG2()) daq_state = STATE_ERROR;
        else daq_state = STATE_READY;
    } else if ((daq_state == STATE_ACQUIRE) && (command == "disarm")) {
        daq_state = STATE_CHANGE;
        if (jfwriter_disarm()) daq_state = STATE_ERROR;
        else daq_state = STATE_READY;
    } else if ((daq_state == STATE_ACQUIRE) && (command == "abort")) {
        daq_state = STATE_CHANGE;
        if (jfwriter_disarm()) daq_state = STATE_ERROR;
        else daq_state = STATE_READY;
    } else response.send(Pistache::Http::Code::Bad_Request);

    if (daq_state != STATE_ERROR) response.send(Pistache::Http::Code::Ok);
    else response.send(Pistache::Http::Code::Bad_Request);

    pthread_mutex_unlock(&daq_state_mutex);
}

void detector_get_all(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    nlohmann::json j;
    j["state"] = state_to_string();
    for (const auto &it: detector_options) {
        (it.second).output(j[it.first]);
    }
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void detector_get(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    if (daq_state == STATE_NOT_INITIALIZED) {
        response.send(Pistache::Http::Code::Bad_Request, "Variables have no meaning before initialization");
        return;
    }

    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;
    if (variable == "keys") {
        for (const auto& it: detector_options)
            j["keys"].push_back(it.first);
    } else if (variable == "status")
        j["state"] = state_to_string();
    else if (variable == "all") {
        j["state"] = state_to_string();
        for (const auto &it: detector_options) {
            (it.second).output(j[it.first]);
        }
    } else if (variable == "help") {
        for (const auto &it: detector_options) {
            j[it.first]["description"] = (it.second).description;
            j[it.first]["min"] = (it.second).min;
            j[it.first]["max"] = (it.second).max;
            j[it.first]["ro"] = (it.second).read_only;
            j[it.first]["allowed_values"] = (it.second).allowed_string_values;
            j[it.first]["units"] = (it.second).units;
            switch (it.second.type) {
                case PARAMETER_FLOAT:
                    j[it.first]["type"] = "float";
                    break;
                case PARAMETER_STRING:
                    j[it.first]["type"] = "string";
                    break;
                case PARAMETER_BOOL:
                    j[it.first]["type"] = "bool";
                    break;
                case PARAMETER_UINT:
                    j[it.first]["type"] = "uint";
                    break;
            }
        }
    } else {
        auto it = detector_options.find(variable);
        if (it == detector_options.end()) {
            response.send(Pistache::Http::Code::Bad_Request, variable + " not found");
            return;
        } else
            it->second.output(j["value"]);
    }
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void detector_set(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    // only one PUT command at the time
    pthread_mutex_lock(&daq_state_mutex);

    if (daq_state != STATE_READY) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Bad_Request, "Detector is not ready for change");
        return;
    }
    auto variable = request.param(":variable").as<std::string>();

    auto it = detector_options.find(variable);
    if (it == detector_options.end()) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Not_Found, variable + " not found");
        return;
    }

    parameter_t param = it->second;

    if (param.read_only) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Bad_Request, variable + " is read only");
        return;
    }
    nlohmann::json json_input;
    try {
        json_input = nlohmann::json::parse(request.body())["value"];
    } catch (nlohmann::json::parse_error &e) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Bad_Request, "Required JSON {value : <param value>}");
        return;
    }

    try {
        if ((param.type == PARAMETER_STRING) && (!param.allowed_string_values.empty())) {
            if (param.allowed_string_values.find(json_input.get<std::string>()) == param.allowed_string_values.end() ) {
                pthread_mutex_unlock(&daq_state_mutex);
                response.send(Pistache::Http::Code::Bad_Request, "Value " + json_input.get<std::string>() + " is not valid for variable " + variable);
                return;
            }
        }
        if ((param.type == PARAMETER_UINT) || (param.type == PARAMETER_FLOAT))  {
            double val = json_input.get<double>();
            if (val < param.min) {
                pthread_mutex_unlock(&daq_state_mutex);
                response.send(Pistache::Http::Code::Bad_Request, "Value " + std::to_string(json_input.get<double>()) +
                " is smaller than min " + std::to_string(param.min));
                return;
            }
            if (val > param.max) {
                pthread_mutex_unlock(&daq_state_mutex);
                response.send(Pistache::Http::Code::Bad_Request, "Value " + std::to_string(json_input.get<double>()) +
                                                                 " is smaller than max " + std::to_string(param.max));
                return;
            }
        }
        param.input(json_input);
    } catch (nlohmann::json::type_error &e) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Not_Found, "Wrong type: " + std::string(e.what()));
    } catch (nlohmann::json::parse_error &e) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Not_Found, "Error in json parser");
    }
    update_summation();
    pthread_mutex_unlock(&daq_state_mutex);
    response.send(Pistache::Http::Code::Ok);
}

void detector_set_multiple(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    // only one PUT command at the time
    pthread_mutex_lock(&daq_state_mutex);

    if (daq_state != STATE_READY) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Bad_Request, "Detector is not ready for change");
        return;
    }
    nlohmann::json json_input;
    try {
        json_input = nlohmann::json::parse(request.body());

        for (auto &it_json: json_input.items()) {
            auto variable = it_json.key();

            auto it = detector_options.find(variable);
            if (it == detector_options.end()) {
                pthread_mutex_unlock(&daq_state_mutex);
                response.send(Pistache::Http::Code::Not_Found, variable + " not found");
                return;
            }

            parameter_t param = it->second;

            if (param.read_only) {
                pthread_mutex_unlock(&daq_state_mutex);
                response.send(Pistache::Http::Code::Bad_Request, variable + " is read only");
                return;
            }


            if ((param.type == PARAMETER_STRING) && (!param.allowed_string_values.empty())) {
                if (param.allowed_string_values.find(it_json.value().get<std::string>()) == param.allowed_string_values.end()) {
                    pthread_mutex_unlock(&daq_state_mutex);
                    response.send(Pistache::Http::Code::Bad_Request,
                                  "Value " + json_input.get<std::string>() + " is not valid for variable " + variable);
                    return;
                }
            }
            if ((param.type == PARAMETER_UINT) || (param.type == PARAMETER_FLOAT)) {
                double val = it_json.value().get<double>();
                if (val < param.min) {
                    pthread_mutex_unlock(&daq_state_mutex);
                    response.send(Pistache::Http::Code::Bad_Request, "Value " + std::to_string(json_input.get<double>()) +
                                                                     " is smaller than min " + std::to_string(param.min));
                    return;
                }
                if (val > param.max) {
                    pthread_mutex_unlock(&daq_state_mutex);
                    response.send(Pistache::Http::Code::Bad_Request, "Value " + std::to_string(json_input.get<double>()) +
                                                                     " is smaller than max " + std::to_string(param.max));
                    return;
                }
            }
            param.input(it_json.value());
        }
    } catch (nlohmann::json::type_error &e) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Not_Found, "Wrong type: " + std::string(e.what()));
    } catch (nlohmann::json::parse_error &e) {
        pthread_mutex_unlock(&daq_state_mutex);
        response.send(Pistache::Http::Code::Not_Found, "Error in json parser");
    }
    update_summation();
    pthread_mutex_unlock(&daq_state_mutex);
    response.send(Pistache::Http::Code::Ok);
}

void detector_state(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    nlohmann::json j;
    j = state_to_string();
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void fetch_preview(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    auto variable = request.param(":variable").as<double>();

    std::vector<uint8_t> *jpeg = new std::vector<uint8_t>;
    update_jpeg_preview(*jpeg, variable);

    auto res = response.send(Pistache::Http::Code::Ok, (char *) jpeg->data(), jpeg->size(), MIME(Image, Jpeg));
    res.then([jpeg](ssize_t bytes) { delete (jpeg); }, Pistache::Async::NoExcept);
}

void fetch_preview_log(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    auto variable = request.param(":variable").as<double>();

    std::vector<uint8_t> *jpeg = new std::vector<uint8_t>;
    update_jpeg_preview_log(*jpeg, variable);

    auto res = response.send(Pistache::Http::Code::Ok, (char *) jpeg->data(), jpeg->size(), MIME(Image, Jpeg));
    res.then([jpeg](ssize_t bytes) { delete (jpeg); }, Pistache::Async::NoExcept);

}

void fetch_spot_count_per_image(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    pthread_mutex_lock(&spots_statistics_mutex);
    nlohmann::json j = spot_count_per_image;
    pthread_mutex_unlock(&spots_statistics_mutex);

    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void fetch_spot_sequence(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    pthread_mutex_lock(&spots_statistics_mutex);
    nlohmann::json j;
    j["sequence"] = spot_statistics_sequence;
    pthread_mutex_unlock(&spots_statistics_mutex);

    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

// HTTP preflight authorization is required for PUT REST calls to be made from JavaScript in a web browser (cross-origin)
void allow(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Pistache::Http::Header::AccessControlAllowMethods>("PUT");
    response.send(Pistache::Http::Code::No_Content);
}

int main() {
    daq_state = STATE_NOT_INITIALIZED;

    default_parameters();

    jfwriter_setup();

    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(PISTACHE_PORT));

    Pistache::Rest::Router router;

    Pistache::Rest::Routes::Put(router, "/command/:command", Pistache::Rest::Routes::bind(&detector_command));
    Pistache::Rest::Routes::Options(router, "/command/:command", Pistache::Rest::Routes::bind(&allow));

    Pistache::Rest::Routes::Put(router, "/config", Pistache::Rest::Routes::bind(&detector_set_multiple));
    Pistache::Rest::Routes::Options(router, "/config", Pistache::Rest::Routes::bind(&allow));
    Pistache::Rest::Routes::Put(router, "/config/:variable", Pistache::Rest::Routes::bind(&detector_set));
    Pistache::Rest::Routes::Options(router, "/config/:variable", Pistache::Rest::Routes::bind(&allow));

    Pistache::Rest::Routes::Get(router, "/config/:variable", Pistache::Rest::Routes::bind(&detector_get));
    Pistache::Rest::Routes::Get(router, "/config", Pistache::Rest::Routes::bind(&detector_get_all));

    // EIGER compatibility - phase out
    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/config/:variable",
                                Pistache::Rest::Routes::bind(&detector_set));
    Pistache::Rest::Routes::Options(router, "/detector/api/" API_VERSION "/config/:variable",
                                    Pistache::Rest::Routes::bind(&allow));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/config/:variable",
                                Pistache::Rest::Routes::bind(&detector_get));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/status/state",
                                Pistache::Rest::Routes::bind(&detector_state));

    Pistache::Rest::Routes::Put(router, "/filewriter/api/" API_VERSION "/config/:variable",
                                Pistache::Rest::Routes::bind(&detector_set));
    Pistache::Rest::Routes::Options(router, "/filewriter/api/" API_VERSION "/config/:variable",
                                    Pistache::Rest::Routes::bind(&allow));
    Pistache::Rest::Routes::Get(router, "/filewriter/api/" API_VERSION "/config/:variable",
                                Pistache::Rest::Routes::bind(&detector_get));

    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/command/:command",
                                Pistache::Rest::Routes::bind(&detector_command));
    Pistache::Rest::Routes::Options(router, "/detector/api/" API_VERSION "/command/:command",
                                    Pistache::Rest::Routes::bind(&allow));

    // There is no stream, but this is needed to interface with DA+
    Pistache::Rest::Routes::Get(router, "/stream/api/" API_VERSION "/status/state",
                                Pistache::Rest::Routes::bind(&detector_state));

    Pistache::Rest::Routes::Get(router, "/preview/:variable", Pistache::Rest::Routes::bind(&fetch_preview));
    Pistache::Rest::Routes::Get(router, "/preview_log/:variable", Pistache::Rest::Routes::bind(&fetch_preview_log));

    // To reload via browser, something has to change in the address
    // So dummy variable x is added - it is not read, nor parsed, so JS can change it at regular intervals
    Pistache::Rest::Routes::Get(router, "/preview/:variable/:x", Pistache::Rest::Routes::bind(&fetch_preview));
    Pistache::Rest::Routes::Get(router, "/preview_log/:variable/:x", Pistache::Rest::Routes::bind(&fetch_preview_log));

    Pistache::Rest::Routes::Get(router, "/spot_count", Pistache::Rest::Routes::bind(&fetch_spot_count_per_image));
    Pistache::Rest::Routes::Get(router, "/spot_sequence", Pistache::Rest::Routes::bind(&fetch_spot_sequence));

    auto opts = Pistache::Http::Endpoint::options().threads(PISTACHE_THREADS);
    Pistache::Http::Endpoint server(addr);
    server.init(opts);

    server.setHandler(router.handler());
    server.serve();
}
