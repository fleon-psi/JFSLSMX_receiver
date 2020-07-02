/*
 * Copyright 2019 International Business Machines
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

#include <iostream>
#include <cmath>

#include "JFReceiver.h"

#ifdef OCACCEL
#include <osnap_tools.h>
#include <libosnap.h>
#include <osnap_hls_if.h>
#include <osnap_types.h>
#else
#include <snap_tools.h>
#include <libsnap.h>
#include <snap_hls_if.h>
#include <snap_types.h>
#endif

struct snap_card *card = NULL;
struct snap_action *action = NULL;

int setup_snap(uint32_t card_number) {
    char device[128];

    // Allocate the card that will be used
#ifdef OCACCEL
    // default is interrupt mode enabled (vs polling)
    snap_action_flag_t action_irq = (snap_action_flag_t) SNAP_ACTION_DONE_IRQ;

    if(card_number == 0)
        snprintf(device, sizeof(device)-1, "IBM,oc-snap");
    else
        snprintf(device, sizeof(device)-1, "/dev/ocxl/IBM,oc-snap.000%d:00:00.1.0", card_number);
#else
    // default is interrupt mode enabled (vs polling)
	snap_action_flag_t action_irq = (snap_action_flag_t) (SNAP_ACTION_DONE_IRQ | SNAP_ATTACH_IRQ);

	snprintf(device, sizeof(device)-1, "/dev/cxl/afu%d.0s", card_number);
#endif
    card = snap_card_alloc_dev(device, SNAP_VENDOR_ID_IBM, SNAP_DEVICE_ID_SNAP);
    if (card == NULL) {
        std::cerr << "FPGA: Failed to attach device " << device << " " << strerror(errno) << std::endl;
        return 1;
    }

    // Attach the action that will be used on the allocated card
    action = snap_attach_action(card, ACTION_TYPE, action_irq, 60);
#ifdef OCACCEL
    if(action_irq)
        snap_action_assign_irq(action, ACTION_IRQ_SRC_LO);
#endif
    if (action == NULL) {
        std::cerr << "FPGA: Failed to attach action to device " << device << " " << strerror(errno) << std::endl;
        snap_card_free(card);
        return 1;
    }
    std::cout << "FPGA: device " << device << " attached" << std::endl;
    return 0;
}

void close_snap() {
    // Detach action + deallocate the card
    snap_detach_action(action);
    snap_card_free(card);
}

void *run_snap_thread(void *in_threadarg) {
    int rc = 0;

    // Control register
    struct snap_job cjob;
    struct rx100G_job mjob;

    mjob.first_frame_number = 1;
    mjob.expected_frames    = experiment_settings.nframes_to_collect;
    mjob.pedestalG0_frames  = experiment_settings.pedestalG0_frames;

    mjob.fpga_mac_addr      = receiver_settings.fpga_mac_addr;   // AA:BB:CC:DD:EE:F1
    mjob.fpga_ipv4_addr     = receiver_settings.fpga_ip_addr;    // 10.1.50.5
    mjob.expected_triggers  = experiment_settings.ntrigger;
    mjob.frames_per_trigger = experiment_settings.nimages_to_write_per_trigger * experiment_settings.summation;
    mjob.delay_per_trigger  = std::lround(experiment_settings.shutter_delay / experiment_settings.frame_time_detector);
    mjob.mode               = experiment_settings.conversion_mode;

    mjob.in_gain_pedestal_data_addr = (uint64_t) gain_pedestal_data;
    mjob.out_frame_buffer_addr      = (uint64_t) frame_buffer;
    mjob.out_frame_status_addr      = (uint64_t) online_statistics;
    mjob.out_jf_packet_headers_addr = (uint64_t) jf_packet_headers;
    // Launch the actual action

    // Call the action will:
    //    write all the registers to the action (MMIO)
    //  + start the action
    //  + wait for completion
    //  + read all the registers from the action (MMIO)

    snap_job_set(&cjob, &mjob, sizeof(mjob), NULL, 0);
    rc = snap_action_sync_execute_job(action, &cjob, TIMEOUT);
    if (rc) std::cerr << "FPGA: Action failed" << std::endl;

    std::cout << "FPGA: Action done" << std::endl;
    //sleep(180);

    // Reset is necessary to purge FIFO for ethernet interface
    //std::cout << "FPGA: Reset ethernet FIFO" << std::endl;
    //mjob.mode = MODE_RESET;
    //snap_job_set(&cjob, &mjob, sizeof(mjob), NULL, 0);
    //rc = snap_action_sync_execute_job(action, &cjob, TIMEOUT);
    //if (rc) std::cerr << "FPGA: Reset failed" << std::endl;

    pthread_exit(0);
}
