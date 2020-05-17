#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <iostream>
#include <map>

#include "../json/single_include/nlohmann/json.hpp"

#include "JFWriter.h"

#define API_VERSION "jf-0.1.0"
#define KEV_OVER_ANGSTROM 12.398

// TODO: Wrong parameters should raise errors

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
    } else if (command == "measure") {
        // Arm + Disarm
        response.send(Pistache::Http::Code::Ok);
    } else
        response.send(Pistache::Http::Code::Not_Found);
}

void update_summation() {
    if (experiment_settings.jf_full_speed) {
        experiment_settings.summation = (int) (experiment_settings.frame_time / 0.0005);
        experiment_settings.frame_time = experiment_settings.summation * 0.0005;
        experiment_settings.count_time = experiment_settings.summation * 0.00047;
    } else {
        experiment_settings.summation = (int) (experiment_settings.frame_time / 0.001);
        experiment_settings.frame_time = experiment_settings.summation * 0.001;
        experiment_settings.count_time = experiment_settings.summation * 0.00097;
    }
    if (experiment_settings.summation == 0) experiment_settings.summation = 1; 
    if (experiment_settings.summation == 1) experiment_settings.pixel_depth = 2;
    else experiment_settings.pixel_depth = 4;

    experiment_settings.nframes_to_collect = experiment_settings.pedestalG0_frames 
        + experiment_settings.summation * experiment_settings.nframes_to_write 
        + experiment_settings.beamline_time_margin / ((experiment_settings.jf_full_speed)? 0.0005 : 0.001);
}

void default_parameters() {
    experiment_settings.jf_full_speed = false;
    experiment_settings.summation = 1;
    experiment_settings.pixel_depth = 2;
    experiment_settings.frame_time = 0.001;
    experiment_settings.nframes_to_write = 0;
    experiment_settings.ntrigger = 1;
    experiment_settings.energy_in_keV = 12.4;
    experiment_settings.pedestalG0_frames = 2000;
    experiment_settings.pedestalG1_frames = 1000;
    experiment_settings.pedestalG2_frames = 1000;
    experiment_settings.conversion_mode = MODE_CONV;

    writer_settings.nthreads = NCARDS * 8; // Spawn 8 writer threads per card
    writer_settings.nlocations = 0;
    writer_settings.timing_trigger = true;

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

void detector_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    std::string variable = request.param(":variable").as<std::string>();
    nlohmann::json json_input = nlohmann::json::parse(request.body());
    nlohmann::json json_output;

    std::cout << "Setting: " << variable << "=" << json_input["value"] << std::endl;

    if (variable == "wavelength") experiment_settings.energy_in_keV = KEV_OVER_ANGSTROM / json_input["value"].get<float>();
    if (variable == "photon_energy") experiment_settings.energy_in_keV = json_input["value"].get<float>() / 1000.0;
    if (variable == "nimages") experiment_settings.nframes_to_write = json_input["value"].get<int>();
    if (variable == "ntrigger") experiment_settings.ntrigger = json_input["value"].get<int>();

    if (variable == "beam_center_x") experiment_settings.beam_x = json_input["value"].get<float>();
    if (variable == "beam_center_y") experiment_settings.beam_y = json_input["value"].get<float>();
    if (variable == "detector_distance") experiment_settings.detector_distance = json_input["value"].get<float>();
    if (variable == "omega_increment") experiment_settings.omega_angle_per_image = json_input["value"].get<float>();
    if (variable == "beamline_time_margin") experiment_settings.beamline_time_margin = json_input["value"].get<float>();

    if (variable == "compression") {
        if (json_input["value"].get<std::string>() == "bslz4") writer_settings.compression = COMPRESSION_BSHUF_LZ4;
        else if (json_input["value"].get<std::string>() == "bszstd") writer_settings.compression = COMPRESSION_BSHUF_ZSTD;
        else writer_settings.compression = COMPRESSION_NONE;
    }

    if (variable == "speed") {
        if (json_input["value"].get<std::string>() == "full") experiment_settings.jf_full_speed = true; 
        if (json_input["value"].get<std::string>() == "half") experiment_settings.jf_full_speed = false;
    }

    if (variable == "mode") {
        if (json_input["value"].get<std::string>() == "converted") experiment_settings.conversion_mode = MODE_CONV;
        if (json_input["value"].get<std::string>() == "raw") experiment_settings.conversion_mode = MODE_RAW;
        if (json_input["value"].get<std::string>() == "pedestalG1") experiment_settings.conversion_mode = MODE_PEDEG1;
        if (json_input["value"].get<std::string>() == "pedestalG2") experiment_settings.conversion_mode = MODE_PEDEG2;
    }

    if (variable == "frame_time") experiment_settings.frame_time = json_input["value"].get<float>();
    if (variable == "pedestalG0_frames") experiment_settings.pedestalG0_frames = json_input["value"].get<int>();
    if (variable == "pedestalG1_frames") experiment_settings.pedestalG1_frames = json_input["value"].get<int>();
    if (variable == "pedestalG2_frames") experiment_settings.pedestalG2_frames = json_input["value"].get<int>();
    
    update_summation();
    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void detector_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;

    if (variable == "wavelength") {j["value"] = KEV_OVER_ANGSTROM/experiment_settings.energy_in_keV; j["value_type"] = "float"; j["unit"] = "Angstrom";}
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
    if (variable == "frame_time") {j["value"] = experiment_settings.frame_time; j["value_type"] = "float"; j["unit"] ={"s"};}
    if (variable == "count_time") {j["value"] = experiment_settings.count_time; j["value_type"] = "float"; j["unit"] ={"s"};}

    // JF specific
    if (variable == "beamline_time_margin") {j["value"] = experiment_settings.beamline_time_margin; j["value_type"] = "float"; j["unit"] = "s";}
    if (variable == "pedestalG0_frames") {j["value"] = experiment_settings.pedestalG0_frames; j["value_type"] = "uint";}
    if (variable == "pedestalG1_frames") {j["value"] = experiment_settings.pedestalG1_frames; j["value_type"] = "uint";}
    if (variable == "pedestalG2_frames") {j["value"] = experiment_settings.pedestalG2_frames; j["value_type"] = "uint";}
    if (variable == "speed") {j["value"] = experiment_settings.jf_full_speed? "full":"half"; j["value_type"] = "string"; j["allowed_values"] ={"full", "half"};}
    if (variable == "mode") {
        j["value_type"] = "string"; j["allowed_values"] ={"converted", "raw", "pedestalG1", "pedestalG2"};
        if (experiment_settings.conversion_mode == MODE_CONV) j["value"] = "converted";
        if (experiment_settings.conversion_mode == MODE_RAW) j["value"] = "raw";
        if (experiment_settings.conversion_mode == MODE_PEDEG1) j["value"] = "pedestalG1";
        if (experiment_settings.conversion_mode == MODE_PEDEG2) j["value"] = "pedestalG2";
    }
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void filewriter_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json json_input = nlohmann::json::parse(request.body());
    nlohmann::json json_output;

    if (variable == "nimages_per_file") writer_settings.images_per_file = json_input["value"].get<int>();
    if (variable == "name_pattern") writer_settings.HDF5_prefix = json_input["value"].get<std::string>();
    if (variable == "format") { 
        if (json_input["value"].get<std::string>() == "binary") writer_settings.write_hdf5 = false;
        if (json_input["value"].get<std::string>() == "hdf5") writer_settings.write_hdf5 = true;
    }
    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void filewriter_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void hello(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.send(Pistache::Http::Code::Ok, "SLS MX JUNGFRAU API: " API_VERSION "\n");
}

void detector_state(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
}

int main() {

    default_parameters();

    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(5232));

    Pistache::Rest::Router router;
    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/command/:command", Pistache::Rest::Routes::bind(&detector_command));
    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&detector_set));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&detector_get));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/status/state", Pistache::Rest::Routes::bind(&detector_state));

    Pistache::Rest::Routes::Put(router, "/filewriter/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&filewriter_set));
    Pistache::Rest::Routes::Get(router, "/filewriter/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&filewriter_get));

    Pistache::Rest::Routes::Get(router, "/", Pistache::Rest::Routes::bind(&hello));

    auto opts = Pistache::Http::Endpoint::options().threads(1);
    Pistache::Http::Endpoint server(addr);
    server.init(opts);
    server.setHandler(router.handler());
    server.serve();
}
