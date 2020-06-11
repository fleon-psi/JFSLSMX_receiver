#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/client.h>
#include <iostream>
#include <map>
#include <cmath>

#include "../json/single_include/nlohmann/json.hpp"

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
enum daq_state_t {STATE_READY, STATE_ACQUIRE, STATE_ERROR, STATE_CHANGE, STATE_NOT_INITIALIZED} daq_state;

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

void full_status(nlohmann::json &j) {
    j["frame_time_detector"] = experiment_settings.frame_time_detector;
    j["count_time_detector"] = experiment_settings.count_time_detector;
    j["frame_time"] = experiment_settings.frame_time;
    j["count_time"] = experiment_settings.count_time;
    j["shutter_delay"] = experiment_settings.shutter_delay;
    j["beamline_delay"] = experiment_settings.beamline_delay;
    j["pedestalG0_frames"] = experiment_settings.pedestalG0_frames;
    j["pedestalG1_frames"] = experiment_settings.pedestalG1_frames;
    j["pedestalG2_frames"] = experiment_settings.pedestalG2_frames;
    j["summation"] = experiment_settings.summation;
    j["pixel_depth"] = experiment_settings.pixel_depth;
    j["ntrigger"] = experiment_settings.ntrigger;
    j["hdf5_prefix"] = writer_settings.HDF5_prefix;
    j["write_hdf5"] = writer_settings.write_hdf5;
    j["jf_full_speed"] = experiment_settings.jf_full_speed;
    j["nframes_to_collect"] = experiment_settings.nframes_to_collect;
    j["nframes_to_write"] = experiment_settings.nframes_to_write;
    j["nframes_to_write_per_trigger"] = experiment_settings.nframes_to_write_per_trigger;

    j["beam_center_x"] = experiment_settings.beam_x;
    j["beam_center_y"] = experiment_settings.beam_y;
    j["detector_distance"] = experiment_settings.detector_distance;
    j["energy_in_keV"] = experiment_settings.energy_in_keV;

    if (writer_settings.compression == JF_COMPRESSION_NONE) j["compression"] = ""; 
    else if (writer_settings.compression == JF_COMPRESSION_BSHUF_LZ4) j["compression"] = "bslz4"; 
    else if (writer_settings.compression == JF_COMPRESSION_BSHUF_ZSTD) j["compression"] = "bszstd"; 

    time_t now;
    time(&now);
    j["pedestalG0_age"] = (uint64_t)(time - time_pedestalG0.tv_sec);
    j["pedestalG1_age"] = (uint64_t)(time - time_pedestalG1.tv_sec);
    j["pedestalG2_age"] = (uint64_t)(time - time_pedestalG2.tv_sec);
    for (int i = 0; i < NCARDS * NMODULES; i++) {
        j["pedestalG0_mean_mod"+std::to_string(i)] = mean_pedestalG0[i];
        j["pedestalG1_mean_mod"+std::to_string(i)] = mean_pedestalG1[i];
        j["pedestalG2_mean_mod"+std::to_string(i)] = mean_pedestalG2[i];
        j["bad_pixels_mod"+std::to_string(i)] = bad_pixels[i];
    } 
    j["state"] = state_to_string();
}

void update_summation() {
    if (experiment_settings.jf_full_speed) {
        if (experiment_settings.frame_time_detector < FRAME_TIME_FULL_SPEED) experiment_settings.frame_time_detector = FRAME_TIME_FULL_SPEED;
//        if (experiment_settings.count_time_detector < COUNT_TIME_FULL_SPEED) experiment_settings.count_time_detector = COUNT_TIME_FULL_SPEED;
    } else {
        if (experiment_settings.frame_time_detector < FRAME_TIME_HALF_SPEED) experiment_settings.frame_time_detector = FRAME_TIME_HALF_SPEED;
//        if (experiment_settings.count_time_detector < COUNT_TIME_HALF_SPEED) experiment_settings.count_time_detector = COUNT_TIME_HALF_SPEED;
    }
    
    experiment_settings.summation = std::lround(experiment_settings.frame_time / experiment_settings.frame_time_detector);

    // Summation
    if (experiment_settings.summation == 0) experiment_settings.summation = 1; 

    experiment_settings.frame_time = experiment_settings.frame_time_detector * experiment_settings.summation;
    experiment_settings.count_time = experiment_settings.count_time_detector * experiment_settings.summation;

    if (experiment_settings.summation == 1) experiment_settings.pixel_depth = 2;
    else experiment_settings.pixel_depth = 4;

    if (experiment_settings.ntrigger == 0) experiment_settings.nframes_to_write = experiment_settings.nframes_to_write_per_trigger;
    else experiment_settings.nframes_to_write = experiment_settings.nframes_to_write_per_trigger * experiment_settings.ntrigger;

    experiment_settings.nframes_to_collect = experiment_settings.pedestalG0_frames 
        + experiment_settings.summation * experiment_settings.nframes_to_write
        + experiment_settings.beamline_delay / experiment_settings.frame_time_detector
        + experiment_settings.shutter_delay * experiment_settings.ntrigger / experiment_settings.frame_time_detector;
}

void default_parameters() {
    experiment_settings.jf_full_speed = false;
    experiment_settings.frame_time_detector = FRAME_TIME_HALF_SPEED;
    experiment_settings.count_time_detector = COUNT_TIME_HALF_SPEED;
    experiment_settings.summation = 1;
    experiment_settings.frame_time = FRAME_TIME_HALF_SPEED;
    experiment_settings.nframes_to_write = 0;
    experiment_settings.ntrigger = 0;
    experiment_settings.energy_in_keV = 12.4;
    experiment_settings.pedestalG0_frames = 2000;
    experiment_settings.pedestalG1_frames = 1000;
    experiment_settings.pedestalG2_frames = 1000;
    experiment_settings.conversion_mode = MODE_CONV;
    
    writer_settings.write_hdf5 = true;
    writer_settings.images_per_file = 1000;
    writer_settings.nthreads = NCARDS * 8; // Spawn 8 writer threads per card
    writer_settings.timing_trigger = true;

    writer_settings.nlocations = 1;
    writer_settings.data_location[0] = "/mnt/n2ssd";
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
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*"); // Necessary for Java Script

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

void detector_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    pthread_mutex_lock(&daq_state_mutex);

    if (daq_state != STATE_READY) {
        response.send(Pistache::Http::Code::Bad_Request);
        pthread_mutex_unlock(&daq_state_mutex);
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
    else if (variable == "omega_start") experiment_settings.omega_start = json_input["value"].get<float>();
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
        // Change of detector speed invalidates pedestal
        if (json_input["value"].get<std::string>() == "full") experiment_settings.jf_full_speed = true; 
        if (json_input["value"].get<std::string>() == "half") experiment_settings.jf_full_speed = false;
        time_pedestalG0.tv_sec = 0;
        time_pedestalG1.tv_sec = 0;
        time_pedestalG2.tv_sec = 0;
    }

    else if (variable == "mode") {
        if (json_input["value"].get<std::string>() == "converted") experiment_settings.conversion_mode = MODE_CONV;
        if (json_input["value"].get<std::string>() == "raw") experiment_settings.conversion_mode = MODE_RAW;
        if (json_input["value"].get<std::string>() == "pedestalG0") experiment_settings.conversion_mode = MODE_PEDEG0;
        if (json_input["value"].get<std::string>() == "pedestalG1") experiment_settings.conversion_mode = MODE_PEDEG1;
        if (json_input["value"].get<std::string>() == "pedestalG2") experiment_settings.conversion_mode = MODE_PEDEG2;
    }

    else if (variable == "frame_time") experiment_settings.frame_time = json_input["value"].get<float>();
    else if (variable == "count_time") ; // This cannot be set

    // Change of internal detector frame time invalidates pedestal for G0 (as this one is collected at actual frame rate)
    else if (variable == "frame_time_detector") {
        experiment_settings.frame_time_detector = json_input["value"].get<float>();
        time_pedestalG0.tv_sec = 0;
    } else if (variable == "count_time_detector") {
    // Change of internal detector count time invalidates pedestal for all gains
        experiment_settings.count_time_detector = json_input["value"].get<float>();
        time_pedestalG0.tv_sec = 0;
        time_pedestalG1.tv_sec = 0;
        time_pedestalG2.tv_sec = 0;
    }

    // Change of number of frames invalidated pedestal
    else if (variable == "pedestalG0_frames") {experiment_settings.pedestalG0_frames = json_input["value"].get<int>(); time_pedestalG0.tv_sec = 0;}
    else if (variable == "pedestalG1_frames") {experiment_settings.pedestalG1_frames = json_input["value"].get<int>(); time_pedestalG1.tv_sec = 0;}
    else if (variable == "pedestalG2_frames") {experiment_settings.pedestalG2_frames = json_input["value"].get<int>(); time_pedestalG2.tv_sec = 0;}
    else {
	 response.send(Pistache::Http::Code::Not_Found, "Key " + variable + " doesn't exists");
         return;
    }
    update_summation();
    pthread_mutex_unlock(&daq_state_mutex);

    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void detector_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    if (daq_state == STATE_NOT_INITIALIZED) {
        response.send(Pistache::Http::Code::Bad_Request, "Variables have no meaning before initialization");
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
    else if (variable == "omega_start") {j["value"] = experiment_settings.omega_start; j["value_type"] = "float"; j["unit"] = "degree";}
    else if (variable == "omega_increment") {j["value"] = experiment_settings.omega_angle_per_image; j["value_type"] = "float"; j["unit"] = "degree";}
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
    else if (variable == "frame_time_detector") {j["value"] = experiment_settings.frame_time_detector; j["value_type"] = "float"; j["unit"] ={"s"};}
    else if (variable == "count_time_detector") {j["value"] = experiment_settings.count_time_detector; j["value_type"] = "float"; j["unit"] ={"s"};}

    // JF specific
    else if (variable == "beamline_delay") {j["value"] = experiment_settings.beamline_delay; j["value_type"] = "float"; j["unit"] = "s";}
    else if (variable == "shutter_delay") {j["value"] = experiment_settings.shutter_delay; j["value_type"] = "float"; j["unit"] = "s";}
    else if (variable == "pedestalG0_frames") {j["value"] = experiment_settings.pedestalG0_frames; j["value_type"] = "uint";}
    else if (variable == "pedestalG1_frames") {j["value"] = experiment_settings.pedestalG1_frames; j["value_type"] = "uint";}
    else if (variable == "pedestalG2_frames") {j["value"] = experiment_settings.pedestalG2_frames; j["value_type"] = "uint";}
    else if (variable == "speed") {j["value"] = experiment_settings.jf_full_speed? "full":"half"; j["value_type"] = "string"; j["allowed_values"] ={"full", "half"};}

    else if (variable == "compression") {
        if (writer_settings.compression == JF_COMPRESSION_NONE) j["value"] = "";
        else if (writer_settings.compression == JF_COMPRESSION_BSHUF_LZ4) j["value"] = "bslz4";
        else if (writer_settings.compression == JF_COMPRESSION_BSHUF_ZSTD) j["value"] = "bszstd";
    } else if (variable == "mode") {
        j["value_type"] = "string"; j["allowed_values"] ={"converted", "raw", "pedestalG0", "pedestalG1", "pedestalG2"};
        if (experiment_settings.conversion_mode == MODE_CONV) j["value"] = "converted";
        if (experiment_settings.conversion_mode == MODE_RAW) j["value"] = "raw";
        if (experiment_settings.conversion_mode == MODE_PEDEG0) j["value"] = "pedestalG0";
        if (experiment_settings.conversion_mode == MODE_PEDEG1) j["value"] = "pedestalG1";
        if (experiment_settings.conversion_mode == MODE_PEDEG2) j["value"] = "pedestalG2";
    } else if (variable == "pedestalG0") {
         // Returns full pedestal array
         response.send(Pistache::Http::Code::Ok, (char *) gain_pedestal.pedeG0, NCARDS * NPIXEL * sizeof(uint16_t), MIME(Application, OctetStream)); 
         return; // Response is already sent, so don't send default response
    } else if (variable == "pedestalG1") {
         response.send(Pistache::Http::Code::Ok, (char *) gain_pedestal.pedeG1, NCARDS * NPIXEL * sizeof(uint16_t), MIME(Application, OctetStream)); 
        return;
    } else if (variable == "pedestalG2") {
         response.send(Pistache::Http::Code::Ok, (char *) gain_pedestal.pedeG2, NCARDS * NPIXEL * sizeof(uint16_t), MIME(Application, OctetStream));
         return;
    } else if (variable == "keys") {
         j = {"wavelength" , "photon_energy", "frame_time", "count_time", "frame_time_detector", "count_time_detector", "beamline_delay", "shutter_delay"};
    } else {
	 response.send(Pistache::Http::Code::Not_Found, "Key " + variable + " doesn't exists");
         return;
    }

    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void filewriter_set(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");

    pthread_mutex_lock(&daq_state_mutex);

    if (daq_state != STATE_READY) {
        response.send(Pistache::Http::Code::Bad_Request);
        pthread_mutex_unlock(&daq_state_mutex);
        return;
    }

    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json json_input = nlohmann::json::parse(request.body());
    nlohmann::json json_output;

    if (variable == "nimages_per_file") writer_settings.images_per_file = json_input["value"].get<int>();
    else if (variable == "name_pattern") writer_settings.HDF5_prefix = json_input["value"].get<std::string>();
    else if (variable == "compression_enabled");
    else if (variable == "format") { 
        if (json_input["value"].get<std::string>() == "binary") writer_settings.write_hdf5 = false;
        if (json_input["value"].get<std::string>() == "hdf5") writer_settings.write_hdf5 = true;
    } else {
	 response.send(Pistache::Http::Code::Not_Found, "Key " + variable + " doesn't exists");
         return;
    }

    pthread_mutex_unlock(&daq_state_mutex);
    response.send(Pistache::Http::Code::Ok, json_output.dump(), MIME(Application, Json));
}

void filewriter_get(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    if (daq_state == STATE_NOT_INITIALIZED) {
        response.send(Pistache::Http::Code::Bad_Request);
        return;
    }
    auto variable = request.param(":variable").as<std::string>();
    nlohmann::json j;
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

void detector_state(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    nlohmann::json j;
    j = state_to_string();
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));            
}

void fetch_preview(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    auto variable = request.param(":variable").as<double>();
    
    std::vector<uint8_t> *jpeg = new std::vector<uint8_t>;
    update_jpeg_preview(*jpeg, variable);

    response.send(Pistache::Http::Code::Ok, (char *)jpeg->data(), jpeg->size(), MIME(Image,Jpeg));

}

void fetch_preview_log(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    auto variable = request.param(":variable").as<double>();
    
    std::vector<uint8_t> *jpeg = new std::vector<uint8_t>;
    update_jpeg_preview_log(*jpeg, variable);

    response.send(Pistache::Http::Code::Ok, (char *)jpeg->data(), jpeg->size(), MIME(Image,Jpeg));

}

void full_detector_state(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    response.headers().add<Pistache::Http::Header::AccessControlAllowOrigin>("*");
    nlohmann::json j;
    full_status(j);
    response.send(Pistache::Http::Code::Ok, j.dump(), MIME(Application, Json));
}

// This is required for PUT REST calls to be made from JavaScript in a web browser
void allow(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
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
    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/command/:command", Pistache::Rest::Routes::bind(&detector_command));
    Pistache::Rest::Routes::Options(router, "/detector/api/" API_VERSION "/command/:command", Pistache::Rest::Routes::bind(&allow));

    Pistache::Rest::Routes::Put(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&detector_set));
    Pistache::Rest::Routes::Options(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&allow));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&detector_get));
    Pistache::Rest::Routes::Get(router, "/detector/api/" API_VERSION "/status/state", Pistache::Rest::Routes::bind(&detector_state));

    Pistache::Rest::Routes::Put(router, "/filewriter/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&filewriter_set));
    Pistache::Rest::Routes::Options(router, "/filewriter/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&allow));
    Pistache::Rest::Routes::Get(router, "/filewriter/api/" API_VERSION "/config/:variable", Pistache::Rest::Routes::bind(&filewriter_get));
    
    // There is no stream, but this is needed to interface with DA+
    Pistache::Rest::Routes::Get(router, "/stream/api/" API_VERSION "/status/state", Pistache::Rest::Routes::bind(&detector_state));

    Pistache::Rest::Routes::Get(router, "/preview/:variable", Pistache::Rest::Routes::bind(&fetch_preview));
    Pistache::Rest::Routes::Get(router, "/preview_log/:variable", Pistache::Rest::Routes::bind(&fetch_preview_log));
    Pistache::Rest::Routes::Get(router, "/", Pistache::Rest::Routes::bind(&full_detector_state));
    
    auto opts = Pistache::Http::Endpoint::options().threads(PISTACHE_THREADS);
    Pistache::Http::Endpoint server(addr);
    server.init(opts);

    server.setHandler(router.handler());
    server.serve();
}
