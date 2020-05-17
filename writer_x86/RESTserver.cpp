#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <iostream>
#include <map>

#include "../json/single_include/nlohmann/json.hpp"

#include "JFWriter.h"

#define API_VERSION "jf-0.1.0"

void detector_command(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto command = request.param(":command").as<std::string>();
    if (command == "arm") {
        // Arm
        response.send(Pistache::Http::Code::Ok);
    } else if (command == "disarm") {
        // Disarm
        response.send(Pistache::Http::Code::Ok);
    } else if (command == "cancel") {
        // Disarm
        response.send(Pistache::Http::Code::Ok);
    } else if (command == "abort") {
        // Disarm
        response.send(Pistache::Http::Code::Ok);
    } else if (command == "status_update") {
        // Status update
        response.send(Pistache::Http::Code::Ok);
    } else if (command == "initialize") {
        // Init
        response.send(Pistache::Http::Code::Ok);
    } else
        response.send(Pistache::Http::Code::Not_Found);
}

void update_summation() {
    if (experiment_settings.jf_full_speed) {
        experiment_settings.summation = (int) (experiment_settings.frame_time / 0.0005);
    } else {
        experiment_settings.summation = (int) (experiment_settings.frame_time / 0.001);
    }
    if (experiment_settings.summation == 0) experiment_settings.summation = 1; 
    if (experiment_settings.summation == 1) experiment_settings.pixel_depth = 2;
    else experiment_settings.pixel_depth = 4;
}

void detector_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    std::string variable = request.param(":variable").as<std::string>();
    nlohmann::json json_input = nlohmann::json::parse(request.body());
    nlohmann::json json_output;

    std::cout << "Setting: " << variable << "=" << json_input["value"] << std::endl;

    if (variable == "wavelength") {experiment_settings.energy_in_keV = 12.4 / json_input["value"].get<float>();}
    if (variable == "photon_energy") {experiment_settings.energy_in_keV = json_input["value"].get<float>() / 1000.0;}
    if (variable == "nimages") {experiment_settings.nframes_to_write = json_input["value"].get<int>();}
    if (variable == "ntrigger") {experiment_settings.ntrigger = json_input["value"].get<int>();}

    if (variable == "beam_center_x") {experiment_settings.beam_x = json_input["value"].get<float>();}
    if (variable == "beam_center_y") {experiment_settings.beam_y = json_input["value"].get<float>();}
    if (variable == "detector_distance") {experiment_settings.detector_distance = json_input["value"].get<float>();}
    if (variable == "omega_increment") {experiment_settings.omega_angle_per_image = json_input["value"].get<float>();}

    if (variable == "compression") {
        if (json_input["value"].get<std::string>() == "bslz4") writer_settings.compression = COMPRESSION_BSHUF_LZ4;
        else if (json_input["value"].get<std::string>() == "bszstd") writer_settings.compression = COMPRESSION_BSHUF_ZSTD;
        else writer_settings.compression = COMPRESSION_NONE;
    }

    if (variable == "speed") {
        if (json_input["value"].get<std::string>() == "full") {experiment_settings.jf_full_speed = true;  experiment_settings.count_time = 0.00047; update_summation();}
        if (json_input["value"].get<std::string>() == "half") {experiment_settings.jf_full_speed = false; experiment_settings.count_time = 0.00097; update_summation();}
    }

    if (variable == "frame_time") { experiment_settings.frame_time = json_input["value"].get<float>(); update_summation();}

    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void detector_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;

    if (variable == "wavelength") {j["value"] = 12.4/experiment_settings.energy_in_keV; j["value_type"] = "float"; j["unit"] = "Angstrom";}
    if (variable == "photon_energy") {j["value"] = experiment_settings.energy_in_keV*1000.0; j["value_type"] = "float"; j["unit"] = "eV";}
    if (variable == "x_pixel_size") {j["value"] = 0.075; j["value_type"] = "float"; j["unit"] = "mm";}
    if (variable == "y_pixel_size") {j["value"] = 0.075; j["value_type"] = "float"; j["unit"] = "mm";}

    if (variable == "beam_center_x") {j["value"] = experiment_settings.beam_x; j["value_type"] = "float"; j["unit"] = "pixel";}
    if (variable == "beam_center_y") {j["value"] = experiment_settings.beam_y; j["value_type"] = "float"; j["unit"] = "pixel";}
    if (variable == "detector_distance") {j["value"] = experiment_settings.detector_distance; j["value_type"] = "float"; j["unit"] = "mm";}

    if (variable == "x_pixels_in_detector") {j["value"] = XPIXEL; j["value_type"] = "uint";}
    if (variable == "y_pixels_in_detector") {j["value"] = YPIXEL; j["value_type"] = "uint";}
    if (variable == "bit_depth_image") {j["value"] = experiment_settings.pixel_depth*8; j["value_type"] = "uint";}
    if (variable == "bit_depth_readout") {j["value"] = 16; j["value_type"] = "uint";}
    if (variable == "description") {j["value"] = "JUNGFRAU 4M"; j["value_type"] = "string";}
    if (variable == "summation") {j["value"] = experiment_settings.summation; j["value_type"] = "string";}
    if (variable == "roi_mode") {j["value"] = "disabled"; j["value_type"] = "string"; j["allowed_values"] ={"disabled"};}

    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void hello(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.send(Pistache::Http::Code::Ok, "SLS MX JUNGFRAU API: " API_VERSION "\n");

}

void detector_state(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
}

int main() {

    experiment_settings.energy_in_keV = 6.0;

    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(5232));

    Pistache::Rest::Router router;
    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/command/:command", Pistache::Rest::Routes::bind(&detector_command));
    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&detector_set));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&detector_get));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/status/state", Pistache::Rest::Routes::bind(&detector_state));
    Pistache::Rest::Routes::Get(router, "/", Pistache::Rest::Routes::bind(&hello));

    auto opts = Pistache::Http::Endpoint::options().threads(1);
    Pistache::Http::Endpoint server(addr);
    server.init(opts);
    server.setHandler(router.handler());
    server.serve();
}
