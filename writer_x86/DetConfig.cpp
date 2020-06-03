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

#include <libssh/libssh.h>

#include <Detector.h>

int trigger_omega() {
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
}


int setup_detector() {
#ifndef OFFLINE
        det->stopDetector();

        det->setStartingFrameNumber(1);
        det->setNumberOfFrames(experiment_settings.nframes_to_collect + DELAY_FRAMES_STOP_AND_QUIT+1);
        if (det->size() != NMODULES * NCARDS) {
            std::cerr << "Mismatch in detector size" << std::endl;
            return 1;
        }

        std::chrono::microseconds frame_time;
        std::chrono::microseconds exp_time;

        if (experiment_settings.jf_full_speed) {
            det->setSpeed(slsDetectorDefs::speedLevel::FULL_SPEED);
            frame_time = std::chrono::microseconds(500);
            exp_time = std::chrono::microseconds(470);
        } else {
            det->setSpeed(slsDetectorDefs::speedLevel::HALF_SPEED);
            frame_time = std::chrono::microseconds(1000);
            exp_time = std::chrono::microseconds(970);
        }
        
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
        
        det->setPeriod(frame_time);
        det->setExptime(exp_time);

        if (writer_settings.timing_trigger)
            det->setTimingMode(slsDetectorDefs::timingMode::TRIGGER_EXPOSURE);
        else
            det->setTimingMode(slsDetectorDefs::timingMode::AUTO_TIMING);
#endif        
        return 0;
}

int trigger_detector() {
#ifndef OFFLINE
        time(&time_datacollection);
        if (writer_settings.timing_trigger) {
            det->startDetector();
            // sleep 200 ms is necessary for SNAP setup
            // TODO: Likely 20-50 us would be enough
            usleep(200000);
            trigger_omega();
        } else {
            usleep(200000);
            det->startDetector();
        } 
#endif
        return 0;
}

int close_detector() {
#ifndef OFFLINE
        det->stopDetector();
        det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);
#endif
        return 0;
}

bool detector_power_status() {
}

int powerup_detector() {
        det->setPowerChip(1);
        sleep(5);
        det->setHighVoltage(0);
        sleep(5);
}

int shutdown_detector() {
        det->stopDetector();
        det->setHighVoltage(0);
        sleep(5);
        det->setPowerChip(0);
        sleep(5);
}

