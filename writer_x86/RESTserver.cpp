#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <iostream>
#include <map>
#include <cmath>

#include "../json/single_include/nlohmann/json.hpp"

#include "JFWriter.h"

#define PEDESTAL_TIME_CUTOFF (60*60) // collect pedestal every 1 hour
#define API_VERSION "jf-0.1.0"
#define KEV_OVER_ANGSTROM 12.398

pthread_mutex_t daq_state_mutex = PTHREAD_MUTEX_INITIALIZER;
enum daq_state_t {STATE_READY, STATE_ACQUIRE, STATE_ERROR, STATE_INITIALIZE, STATE_NA} daq_state;

void update_summation() {
    if (experiment_settings.jf_full_speed) {
        experiment_settings.frame_time_detector = 0.0005;
        experiment_settings.count_time_detector = 0.00047;
    } else {
        experiment_settings.frame_time_detector = 0.001;
        experiment_settings.count_time_detector = 0.00097;
    }
    
    experiment_settings.summation = std::lround(experiment_settings.frame_time / experiment_settings.frame_time_detector);

    // Summation
    if (experiment_settings.summation == 0) experiment_settings.summation = 1; 

    experiment_settings.frame_time = experiment_settings.frame_time_detector * experiment_settings.summation;
    experiment_settings.count_time = experiment_settings.count_time_detector * experiment_settings.summation;

    if (experiment_settings.summation == 1) experiment_settings.pixel_depth = 2;
    else experiment_settings.pixel_depth = 4;

    experiment_settings.nframes_to_write = experiment_settings.nframes_to_write_per_trigger * experiment_settings.ntrigger;

    experiment_settings.nframes_to_collect = experiment_settings.pedestalG0_frames 
        + experiment_settings.summation * experiment_settings.nframes_to_write
        + experiment_settings.beamline_delay / experiment_settings.frame_time_detector
        + experiment_settings.shutter_delay * experiment_settings.ntrigger / experiment_settings.frame_time_detector;
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
    
    writer_settings.write_hdf5 = true;
    writer_settings.images_per_file = 1000;
    writer_settings.nthreads = NCARDS * 8; // Spawn 8 writer threads per card
    writer_settings.nlocations = 0;
    writer_settings.timing_trigger = true;

    writer_settings.nlocations = 4;
    writer_settings.data_location[0] = "/mnt/n0ram/";
    writer_settings.data_location[1] = "/mnt/n1ram/";
    writer_settings.data_location[2] = "/mnt/n2ram/";
    writer_settings.data_location[3] = "/mnt/n3ram/";
    writer_settings.main_location = "/mnt/n2ssd/";

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

// TODO: Wrong parameters should raise errors
void detector_command(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto command = request.param(":command").as<std::string>();
    if (command == "arm") {
        if (daq_state == STATE_READY) {
        // Arm
            std::cout << "Frames to collect:    " << experiment_settings.nframes_to_collect << std::endl;
            std::cout << "Frames to write:      " << experiment_settings.nframes_to_write << std::endl;
            std::cout << "Conversion mode:      " << (uint32_t) experiment_settings.conversion_mode << std::endl;
            std::cout << "Summation:            " << experiment_settings.summation << std::endl;
            std::cout << "Pixel depth:          " << experiment_settings.pixel_depth << std::endl;
            std::cout << "Pedestal G0 frames:   " << experiment_settings.pedestalG0_frames << std::endl;
            std::cout << "Pedestal G1 frames:   " << experiment_settings.pedestalG1_frames << std::endl;
            std::cout << "Pedestal G2 frames:   " << experiment_settings.pedestalG2_frames << std::endl;
            std::cout << "Ntrigger:             " << experiment_settings.ntrigger << std::endl;
            std::cout << "Count time:           " << experiment_settings.count_time << std::endl;
            std::cout << "Frame time:           " << experiment_settings.frame_time << std::endl;
            std::cout << "Beamline delay:       " << experiment_settings.beamline_delay << std::endl;
            std::cout << "Shutter delay:        " << experiment_settings.shutter_delay << std::endl;
            std::cout << "Full speed:           " << experiment_settings.jf_full_speed << std::endl;
            std::cout << "Name pattern:         " << writer_settings.HDF5_prefix << std::endl;
            std::cout << "Mode:                 " << writer_settings.write_hdf5 << std::endl;
            daq_state = STATE_ACQUIRE;

            // If pedestal is old (or settings changed), need to update it
            time_t now;
            time(&now);
            if (((long)(time - time_pedestalG0) > PEDESTAL_TIME_CUTOFF) 
                && (experiment_settings.pedestalG0_frames == 0)) 
                jfwriter_pedestalG0();
            if (((long)(time - time_pedestalG1) > PEDESTAL_TIME_CUTOFF) 
                || ((long)(time - time_pedestalG2) > PEDESTAL_TIME_CUTOFF)) {
                jfwriter_pedestalG1(); 
                jfwriter_pedestalG2();
            }

            // Arm
            jfwriter_arm();
            std::cout << "Arm done" << std::endl;
            response.send(Pistache::Http::Code::Ok);
        } else {
            response.send(Pistache::Http::Code::Bad_Request);
        }
    } else if (command == "disarm") {
        if (daq_state == STATE_ACQUIRE) {
            jfwriter_disarm();
            std::cout << "Disarm done" << std::endl;
            // Disarm done
            daq_state = STATE_READY;
            response.send(Pistache::Http::Code::Ok);
        } else {
            response.send(Pistache::Http::Code::Bad_Request);
        }
    } else if (command == "cancel") {
        if (daq_state == STATE_ACQUIRE) {
            std::cout << "Disarm" << std::endl;
            jfwriter_disarm();
            daq_state = STATE_READY;

            // Disarm
            response.send(Pistache::Http::Code::Ok);
        } else {
            response.send(Pistache::Http::Code::Bad_Request);
        }
    } else if (command == "abort") {
        if (daq_state == STATE_ACQUIRE) {
            jfwriter_disarm();
            daq_state = STATE_READY;
            std::cout << "Disarm" << std::endl;
            // Disarm
            response.send(Pistache::Http::Code::Ok);
        } else {
            response.send(Pistache::Http::Code::Bad_Request);
        }

    } else if (command == "status_update") {
        // Status update
        response.send(Pistache::Http::Code::Ok);
    } else if (command == "initialize") {
        std::cout << "Initialize" << std::endl;
        if (daq_state == STATE_ACQUIRE) {
            // Disarm
            jfwriter_disarm();
            std::cout << "Disarm done" << std::endl;
        }
        daq_state = STATE_INITIALIZE;

        // Init
        default_parameters();
#ifndef OFFLINE
        jfwriter_pedestalG0();        
        jfwriter_pedestalG1();        
        jfwriter_pedestalG2();        
#endif
        daq_state = STATE_READY;

        response.send(Pistache::Http::Code::Ok);
    } else
        response.send(Pistache::Http::Code::Not_Found);
}

void detector_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    if (daq_state != STATE_READY) {
        response.send(Pistache::Http::Code::Bad_Request);
        return;
    }

    std::string variable = request.param(":variable").as<std::string>();
    nlohmann::json json_input = nlohmann::json::parse(request.body());
    nlohmann::json json_output;

    if (variable == "wavelength") experiment_settings.energy_in_keV = KEV_OVER_ANGSTROM / json_input["value"].get<float>();
    else if (variable == "photon_energy") experiment_settings.energy_in_keV = json_input["value"].get<float>() / 1000.0;
    else if (variable == "nimages") experiment_settings.nframes_to_write_per_trigger = json_input["value"].get<int>();
    else if (variable == "ntrigger") experiment_settings.ntrigger = json_input["value"].get<int>();

    else if (variable == "beam_center_x") experiment_settings.beam_x = json_input["value"].get<float>();
    else if (variable == "beam_center_y") experiment_settings.beam_y = json_input["value"].get<float>();
    else if (variable == "detector_distance") experiment_settings.detector_distance = json_input["value"].get<float>();
    else if (variable == "omega_increment") experiment_settings.omega_angle_per_image = json_input["value"].get<float>();
    else if (variable == "total_flux") experiment_settings.total_flux = json_input["value"].get<float>();
    else if (variable == "beam_size_x") experiment_settings.beam_size_x = json_input["value"].get<float>();
    else if (variable == "beam_size_y") experiment_settings.beam_size_y = json_input["value"].get<float>();

    else if (variable == "beamline_delay") experiment_settings.beamline_delay = json_input["value"].get<float>();
    else if (variable == "shutter_delay") experiment_settings.shutter_delay = json_input["value"].get<float>();

    else if (variable == "compression") {
        if (json_input["value"].get<std::string>() == "bslz4") writer_settings.compression = JF_COMPRESSION_BSHUF_LZ4;
        else if (json_input["value"].get<std::string>() == "bszstd") writer_settings.compression = JF_COMPRESSION_BSHUF_ZSTD;
        else writer_settings.compression = JF_COMPRESSION_NONE;
    }

    else if (variable == "speed") {
        if (json_input["value"].get<std::string>() == "full") experiment_settings.jf_full_speed = true; 
        if (json_input["value"].get<std::string>() == "half") experiment_settings.jf_full_speed = false;
        time_pedestalG0 = 0;
        time_pedestalG1 = 0;
        time_pedestalG2 = 0;
    }

    else if (variable == "mode") {
        if (json_input["value"].get<std::string>() == "converted") experiment_settings.conversion_mode = MODE_CONV;
        if (json_input["value"].get<std::string>() == "raw") experiment_settings.conversion_mode = MODE_RAW;
        if (json_input["value"].get<std::string>() == "pedestalG1") experiment_settings.conversion_mode = MODE_PEDEG1;
        if (json_input["value"].get<std::string>() == "pedestalG2") experiment_settings.conversion_mode = MODE_PEDEG2;
    }

    else if (variable == "frame_time") experiment_settings.frame_time = json_input["value"].get<float>();
    else if (variable == "pedestalG0_frames") {experiment_settings.pedestalG0_frames = json_input["value"].get<int>(); time_pedestalG0 = 0;}
    else if (variable == "pedestalG1_frames") {experiment_settings.pedestalG1_frames = json_input["value"].get<int>(); time_pedestalG1 = 0;}
    else if (variable == "pedestalG2_frames") {experiment_settings.pedestalG2_frames = json_input["value"].get<int>(); time_pedestalG2 = 0;}
    else {
	 response.send(Pistache::Http::Code::Not_Found, "Key " + variable + " doesn't exists");
         return;
    }
    update_summation();
    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void detector_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    if (daq_state == STATE_NA) {
        response.send(Pistache::Http::Code::Bad_Request);
        return;
    }

    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;

    if (variable == "wavelength") {j["value"] = KEV_OVER_ANGSTROM/experiment_settings.energy_in_keV; j["value_type"] = "float"; j["unit"] = "Angstrom";}
    else if (variable == "photon_energy") {j["value"] = experiment_settings.energy_in_keV*1000.0; j["value_type"] = "float"; j["unit"] = "eV";}
    else if (variable == "x_pixel_size") {j["value"] = 0.075; j["value_type"] = "float"; j["unit"] = "mm";}
    else if (variable == "y_pixel_size") {j["value"] = 0.075; j["value_type"] = "float"; j["unit"] = "mm";}
    else if (variable == "beam_center_x") {j["value"] = experiment_settings.beam_x; j["value_type"] = "float"; j["unit"] = "pixel";}
    else if (variable == "beam_center_y") {j["value"] = experiment_settings.beam_y; j["value_type"] = "float"; j["unit"] = "pixel";}
    else if (variable == "detector_distance") {j["value"] = experiment_settings.detector_distance; j["value_type"] = "float"; j["unit"] = "mm";}
    else if (variable == "beam_size_x") {j["value"] = experiment_settings.beam_size_x; j["value_type"] = "float"; j["unit"] = "um";}
    else if (variable == "beam_size_y") {j["value"] = experiment_settings.beam_size_y; j["value_type"] = "float"; j["unit"] = "um";}
    else if (variable == "total_flux") {j["value"] = experiment_settings.total_flux; j["value_type"] = "float"; j["unit"] = "1/s";}
    else if (variable == "transmission") {j["value"] = experiment_settings.transmission; j["value_type"] = "float";}


    else if (variable == "x_pixels_in_detector") {j["value"] = XPIXEL; j["value_type"] = "uint";}
    else if (variable == "y_pixels_in_detector") {j["value"] = YPIXEL; j["value_type"] = "uint";}
    else if (variable == "bit_depth_image") {j["value"] = experiment_settings.pixel_depth*8; j["value_type"] = "uint";}
    else if (variable == "bit_depth_readout") {j["value"] = 16; j["value_type"] = "uint";}
    else if (variable == "description") {j["value"] = "JUNGFRAU 4M"; j["value_type"] = "string";}
    else if (variable == "summation") {j["value"] = experiment_settings.summation; j["value_type"] = "string";}
    else if (variable == "roi_mode") {j["value"] = "disabled"; j["value_type"] = "string"; j["allowed_values"] ={"disabled"};}
    else if (variable == "frame_time") {j["value"] = experiment_settings.frame_time; j["value_type"] = "float"; j["unit"] ={"s"};}
    else if (variable == "count_time") {j["value"] = experiment_settings.count_time; j["value_type"] = "float"; j["unit"] ={"s"};}

    // JF specific
    else if (variable == "beamline_delay") {j["value"] = experiment_settings.beamline_delay; j["value_type"] = "float"; j["unit"] = "s";}
    else if (variable == "shutter_delay") {j["value"] = experiment_settings.shutter_delay; j["value_type"] = "float"; j["unit"] = "s";}
    else if (variable == "pedestalG0_frames") {j["value"] = experiment_settings.pedestalG0_frames; j["value_type"] = "uint";}
    else if (variable == "pedestalG1_frames") {j["value"] = experiment_settings.pedestalG1_frames; j["value_type"] = "uint";}
    else if (variable == "pedestalG2_frames") {j["value"] = experiment_settings.pedestalG2_frames; j["value_type"] = "uint";}
    else if (variable == "speed") {j["value"] = experiment_settings.jf_full_speed? "full":"half"; j["value_type"] = "string"; j["allowed_values"] ={"full", "half"};}
    else if (variable == "mode") {
        j["value_type"] = "string"; j["allowed_values"] ={"converted", "raw", "pedestalG1", "pedestalG2"};
        if (experiment_settings.conversion_mode == MODE_CONV) j["value"] = "converted";
        if (experiment_settings.conversion_mode == MODE_RAW) j["value"] = "raw";
        if (experiment_settings.conversion_mode == MODE_PEDEG1) j["value"] = "pedestalG1";
        if (experiment_settings.conversion_mode == MODE_PEDEG2) j["value"] = "pedestalG2";
    } else if (variable == "mean_pedestalG0") {
        double out[NMODULES*NCARDS];        
        mean_pedeG0(out);
        double sum;
        for (int i = 0; i < NMODULES * NCARDS; i++) {
           sum += out[i];
           j["mod"+std::to_string(i)] = out[i];
        }
        j["all"] = sum / (NMODULES * NCARDS);
    } else if (variable == "mean_pedestalG1") {
        double out[NMODULES*NCARDS];        
        mean_pedeG1(out);
        double sum;
        for (int i = 0; i < NMODULES * NCARDS; i++) {
           sum += out[i];
           j["mod"+std::to_string(i)] = out[i];
        }
        j["all"] = sum / (NMODULES * NCARDS);
    } else if (variable == "mean_pedestalG2") {
        double out[NMODULES*NCARDS];        
        mean_pedeG2(out);
        double sum;
        for (int i = 0; i < NMODULES * NCARDS; i++) {
           sum += out[i];
           j["mod"+std::to_string(i)] = out[i];
        }
        j["all"] = sum / (NMODULES * NCARDS);
    } else if (variable == "bad_pixels") {
        size_t out[NMODULES*NCARDS];        
        count_bad_pixel(out);
        size_t sum;
        for (int i = 0; i < NMODULES * NCARDS; i++) {
           sum += out[i];
           j["mod"+std::to_string(i)] = out[i];
        }
        j["all"] = sum;
    } else if (variable == "pedestalG0") {
         response.send(Pistache::Http::Code::Ok, (char *) gain_pedestal.pedeG0, NCARDS * NPIXEL * sizeof(uint16_t), MIME(Application, OctetStream));    
    } else if (variable == "pedestalG1") {
         response.send(Pistache::Http::Code::Ok, (char *) gain_pedestal.pedeG1, NCARDS * NPIXEL * sizeof(uint16_t), MIME(Application, OctetStream));    
    } else if (variable == "pedestalG2") {
         response.send(Pistache::Http::Code::Ok, (char *) gain_pedestal.pedeG2, NCARDS * NPIXEL * sizeof(uint16_t), MIME(Application, OctetStream));
    }
    else {
	 response.send(Pistache::Http::Code::Not_Found, "Key " + variable + " doesn't exists");
         return;
    }

    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void filewriter_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    if (daq_state != STATE_READY) {
        response.send(Pistache::Http::Code::Bad_Request);
        return;
    }

    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json json_input = nlohmann::json::parse(request.body());
    nlohmann::json json_output;

    if (variable == "nimages_per_file") writer_settings.images_per_file = json_input["value"].get<int>();
    else if (variable == "name_pattern") writer_settings.HDF5_prefix = json_input["value"].get<std::string>();
    else if (variable == "format") { 
        if (json_input["value"].get<std::string>() == "binary") writer_settings.write_hdf5 = false;
        if (json_input["value"].get<std::string>() == "hdf5") writer_settings.write_hdf5 = true;
    } else {
	 response.send(Pistache::Http::Code::Not_Found, "Key " + variable + " doesn't exists");
         return;
    }
    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void filewriter_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    if (daq_state == STATE_NA) {
        response.send(Pistache::Http::Code::Bad_Request);
        return;
    }

    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void hello(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    std::cout << "Root accessed" << std::endl;
    response.send(Pistache::Http::Code::Ok, "SLS MX JUNGFRAU API: " API_VERSION "\n");
}

void detector_state(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    nlohmann::json j;
    switch (daq_state) {
        case STATE_READY:
            j = {"ready"};
            break;
        case STATE_ACQUIRE:
            j = {"acquire"};
            break;
        case STATE_ERROR:
            j = {"error"};
            break;
        case STATE_NA:
            j = {"na"};
            break;
    }
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));            
}

int main() {
    daq_state = STATE_NA;

    default_parameters();

    jfwriter_setup();
    
    std::cout << "Server running"<< std::endl;
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
