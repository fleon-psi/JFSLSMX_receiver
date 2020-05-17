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
        det->stopDetector();

        det->setStartingFrameNumber(1);
        det->setNumberOfFrames(experiment_settings.nframes_to_collect + DELAY_FRAMES_STOP_AND_QUIT+1);
        if (det->size() != NMODULES * NCARDS) {
            std::cerr << "Mismatch in detector size" << std::endl;
            return 1;
        }
        
        if (experiment_settings.jf_full_speed) {
            det->setSpeed(slsDetectorDefs::speedLevel::FULL_SPEED);
            std::chrono::microseconds frame_time{500};
            std::chrono::microseconds exp_time{470};
            det->setPeriod(frame_time);
            det->setExptime(exp_time);
        } else {
            det->setSpeed(slsDetectorDefs::speedLevel::HALF_SPEED);
            std::chrono::microseconds frame_time{1000};
            std::chrono::microseconds exp_time{970};
            det->setPeriod(frame_time);
            det->setExptime(exp_time);
        }
        
        if (experiment_settings.conversion_mode == MODE_PEDEG1) 
            det->setSettings(slsDetectorDefs::detectorSettings::FORCESWITCHG1);
        else if (experiment_settings.conversion_mode == MODE_PEDEG2) 
            det->setSettings(slsDetectorDefs::detectorSettings::FORCESWITCHG2);
        else det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);

        if (writer_settings.timing_trigger)
            det->setTimingMode(slsDetectorDefs::timingMode::TRIGGER_EXPOSURE);
        else
            det->setTimingMode(slsDetectorDefs::timingMode::AUTO_TIMING);
        
        return 0;
}

int trigger_detector() {
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
        return 0;
}

int close_detector() {
        det->stopDetector();
        det->setSettings(slsDetectorDefs::detectorSettings::DYNAMICGAIN);
        return 0;
}
