#ifndef JFAPP_H_
#define JFAPP_H_

#include <stdlib.h>
#include <string>
#include <pthread.h>
#include <lz4.h>
#include <infiniband/verbs.h>

#include "action_rx100G.h"

#define COMPOSED_IMAGE_SIZE (514L*1030L*NMODULES)
//#define RDMA_BUFFER_MAX_ELEM_SIZE (COMPOSED_IMAGE_SIZE*si)

#define TCPIP_CONN_MAGIC_NUMBER 123434L
#define TCPIP_DONE_MAGIC_NUMBER  56789L

// Settings exchanged between writer and receiver
struct experiment_settings_t {
	uint8_t  conversion_mode;

	uint64_t nframes_to_collect;
	uint64_t nimages_to_write;
	uint64_t nimages_to_write_per_trigger;
        uint16_t ntrigger;

	uint32_t pedestalG0_frames;
	uint32_t pedestalG1_frames;
	uint32_t pedestalG2_frames;
        uint32_t summation;

        bool     jf_full_speed;
        double   count_time;            // in s Beamline
        double   frame_time;            // in s Beamline
        double   frame_time_detector;   // in s Detector
	double   count_time_detector;   // in s Detector
        size_t   pixel_depth;           // in byte

	double   energy_in_keV;         // in keV
        double   beam_x;                // in pixel
        double   beam_y;                // in pixel
        double   detector_distance;     // in mm
        double   transmission;          // 1.0 = full transmission
        double   total_flux;           // in e/s
        double   omega_start;           // in degrees
        double   omega_angle_per_image; // in degrees
        double   beam_size_x;           // in micron
        double   beam_size_y;           // in micron
        double   beamline_delay; // in seconds delay between arm succeed and trigger opening (maximal)
        double   shutter_delay;  // in seconds delay between trigger and shutter

        double   strong_pixel_value;
        double sample_temperature; // in K
};

struct receiver_output_t {
       uint64_t frame_when_trigger_observed;
       uint64_t packets_collected_ok;
};

// Settings for IB connection
struct ib_comm_settings_t {
	uint16_t dlid;
	uint32_t qp_num;
	uint32_t rq_psn;
	uint32_t frame_buffer_rkey;
	uint64_t frame_buffer_remote_addr;
};

// IB context
struct ib_settings_t {
	// IB data structures
	ibv_mr *buffer_mr;
	ibv_context *context;
	ibv_pd *pd;
	ibv_cq *cq;
	ibv_qp *qp;
	ibv_port_attr port_attr;
};

// IB Verbs function wrappers
int setup_ibverbs(ib_settings_t &settings, std::string ib_device_name, size_t send_queue_size, size_t receive_queue_size);
int switch_to_rtr(ib_settings_t &settings, uint32_t rq_psn, uint16_t dlid, uint32_t dest_qp_num);
int switch_to_rts(ib_settings_t &settings, uint32_t sq_psn);
int switch_to_init(ib_settings_t &settings);
int switch_to_reset(ib_settings_t &settings);
int close_ibverbs(ib_settings_t &settings);

#endif // JFAPP_H_
