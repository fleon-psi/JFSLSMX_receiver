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


#include <cmath>
#include <libssh/libssh.h>
#include <pistache/client.h>
#include <pistache/http.h>
#include <Detector.h>
#include "JFWriter.h"

int trigger_rpi() {
    int retval = 0;

    Pistache::Http::Client client;

    auto opts = Pistache::Http::Client::options().threads(1).maxConnectionsPerHost(8);
    client.init(opts);

    std::vector<Pistache::Async::Promise<Pistache::Http::Response>> responses;

    auto resp = client.get("http://mx-jungfrau-rpi-1:5000/trigger").send();
    resp.then(
	[&](Pistache::Http::Response response) {
           retval = 0;
         },
	[&](std::exception_ptr exc) {
           std::cerr << "Cannot trigger RPi" << std::endl;
           retval = 1;
        });
    responses.push_back(std::move(resp));

    auto sync = Pistache::Async::whenAll(responses.begin(), responses.end());
    Pistache::Async::Barrier<std::vector<Pistache::Http::Response>> barrier(sync);
    barrier.wait_for(std::chrono::seconds(1));

    client.shutdown();
    return retval;
}


/* int trigger_omega() {
	ssh_session my_ssh_session;	
	my_ssh_session = ssh_new();
        if (my_ssh_session == NULL) {
             std::cerr << "Failure creating SSH session" << std::endl;
             return 1;
        }
        int port = 22;
        ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, "10.10.10.9");
        ssh_options_set(my_ssh_session, SSH_OPTIONS_PORT, &port);
        ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, "pi");

        int rc = ssh_connect(my_ssh_session);
        if (rc != SSH_OK) {
             std::cerr << "Failure in SSH connection to Omega Onion2" << std::endl;
             return 1;
        }

        //enum ssh_known_hosts_e state;
        //state = ssh_session_is_known_server(session);
        //if (state != SSH_KNOWN_HOSTS_OK) {
        //     std::cerr << "Failure in SSH connection to Omega Onion2 (key unknown)" << std::endl;
        //     ssh_disconnect(my_ssh_session);
       	//     ssh_free(my_ssh_session);
        //     return 1;
        //}

        rc = ssh_userauth_password(my_ssh_session, NULL, "filipspi");
        if (rc != SSH_AUTH_SUCCESS) {
             std::cerr << "Failure in SSH connection to Omega Onion2 (auth error)" << std::endl;
             ssh_disconnect(my_ssh_session);
       	     ssh_free(my_ssh_session);
             return 1;
       	}

        ssh_channel my_ssh_channel = ssh_channel_new(my_ssh_session);
        if (my_ssh_channel == NULL) { 
             std::cerr << "Failure in SSH connection to Omega Onion2 (cannot open channel)" << std::endl;
             ssh_disconnect(my_ssh_session);
             ssh_free(my_ssh_session);
             return 1;
        }


        rc = ssh_channel_open_session(my_ssh_channel);
        if (rc != SSH_OK) {
             std::cerr << "Failure in SSH connection to Omega Onion2 (cannot open session)" << std::endl;
             ssh_disconnect(my_ssh_session);
             ssh_free(my_ssh_session);
             return 1;
        }

 
        rc = ssh_channel_request_exec(my_ssh_channel, "bash run");
        if (rc != SSH_OK) {
             std::cerr << "Failure in SSH connection to Omega Onion2 (cannot exec command)" << std::endl;
             ssh_disconnect(my_ssh_session);
             ssh_free(my_ssh_session);
             return 1;
        }
        ssh_channel_send_eof(my_ssh_channel);
        ssh_channel_close(my_ssh_channel);
        ssh_channel_free(my_ssh_channel);
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        std::cout << "SSH OK" << std::endl;
        return 0;
} */


bool is_detector_idle() {
    auto result = det->getDetectorStatus();
    if (!result.equal()) return false;
    auto val = result.squash(slsDetectorDefs::runStatus::ERROR);
    if ((val == slsDetectorDefs::runStatus::IDLE) ||
    (val == slsDetectorDefs::runStatus::STOPPED) ||
    (val == slsDetectorDefs::runStatus::RUN_FINISHED)) return true;
    else return false;
}

void stop_detector() {
    if (!is_detector_idle()) {
        det->stopDetector();
        while (!is_detector_idle()) usleep(10000);
    }
}

int setup_detector() {
#ifndef OFFLINE
        std::cout << "Stopping detector" << std::endl;
        stop_detector();
        std::cout << "Reset frame number" << std::endl;
        det->setStartingFrameNumber(1);
        std::cout << "Set number of frames" << std::endl;
        det->setNumberOfFrames(experiment_settings.nframes_to_collect + DELAY_FRAMES_STOP_AND_QUIT+1);
        std::cout << "Check detector size" << std::endl;
        if (det->size() != NMODULES * NCARDS) {
            std::cerr << "Mismatch in detector size" << std::endl;
            return 1;
        }

        std::chrono::microseconds frame_time = std::chrono::microseconds(std::lround(experiment_settings.frame_time_detector*1e6));
        std::chrono::microseconds exp_time = std::chrono::microseconds(std::lround(experiment_settings.count_time_detector*1e6));

        std::cout << "Set speed" << std::endl;
        if (experiment_settings.jf_full_speed)
            det->setSpeed(slsDetectorDefs::speedLevel::FULL_SPEED);
        else
            det->setSpeed(slsDetectorDefs::speedLevel::HALF_SPEED);

        std::cout << "Set mode" << std::endl;
        if (experiment_settings.conversion_mode == MODE_PEDEG1) {
            frame_time = std::chrono::milliseconds(10); // 100 Hz
            det->setSettings(slsDetectorDefs::detectorSettings::FORCESWITCHG1);
        }
        else if (experiment_settings.conversion_mode == MODE_PEDEG2) {
            frame_time = std::chrono::milliseconds(10); // 100 Hz
            det->setSettings(slsDetectorDefs::detectorSettings::FORCESWITCHG2);
        }
        else
            det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);
        std::cout << "Set frame/exp time" << std::endl;
        det->setPeriod(frame_time);
        det->setExptime(exp_time);

        std::cout << "Set timing" << std::endl;
        if (writer_settings.timing_trigger)
            det->setTimingMode(slsDetectorDefs::timingMode::TRIGGER_EXPOSURE);
        else
            det->setTimingMode(slsDetectorDefs::timingMode::AUTO_TIMING);
#endif        
        return 0;
}

int trigger_detector() {
#ifndef OFFLINE
        if (writer_settings.timing_trigger) {
            std::cout << "Start detector" << std::endl;
            det->startDetector();
            // sleep 200 ms is necessary for SNAP setup
            // TODO: Likely 20-50 us would be enough
            usleep(500000);
            trigger_rpi();
        } else {
            std::cout << "Start detector" << std::endl;
            usleep(500000);
            det->startDetector();
        } 
#endif
        return 0;
}

int close_detector() {
#ifndef OFFLINE
        std::cout << "Stop detector" << std::endl;
        stop_detector();
        std::cout << "Return to dynamic gain" << std::endl;
        det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);
#endif
        return 0;
}

bool detector_power_status() {
    return false;
}

int powerup_detector() {
#ifndef OFFLINE
    det->setPowerChip(1);
    sleep(5);
    det->setHighVoltage(0);
    sleep(5);
#endif
    return 0;
}

int shutdown_detector() {
#ifndef OFFLINE
   det->stopDetector();
   det->setHighVoltage(0);
   sleep(5);
   det->setPowerChip(0);
   sleep(5);
#endif
   return 0;
}

