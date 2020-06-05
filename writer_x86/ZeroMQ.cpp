#include <zmq.h>
#include "../json/single_include/nlohmann/json.hpp"
#include "JFWriter.h"

#define ZMQ_PREFIX "ipc:///tmp/JFWriter/"
void *zeromq_context;

void my_free (void *data, void *hint)
{
     free (data);
}

int setup_zeromq_pull_socket(void **socket, int number) {
    socket[0] = zmq_socket(zeromq_context, ZMQ_PULL);
    std::string conn_name = ZMQ_PREFIX + std::to_string(number);
    int rc = zmq_bind(socket[0], conn_name.c_str());
    if (rc != 0) return rc;
    return 0;
}

int close_zeromq_pull_socket(void **socket) {
    zmq_close(*socket);
    *socket = NULL;
    return 0;
}

int setup_zeromq_context() {
    zeromq_context = zmq_ctx_new();
    int rc = zmq_ctx_set(zeromq_context, ZMQ_IO_THREADS, 4);
    if (rc != 0) return rc;
    return 0;
}

int close_zeromq_context() {
    zmq_ctx_destroy(zeromq_context);
    zeromq_context = NULL;
    return 0;
}

int setup_zeromq_sockets(void **socket) {
    for (int i = 0; i < NZEROMQ; i++) {
        socket[i] = zmq_socket(zeromq_context, ZMQ_PUSH);
        std::string conn_name = ZMQ_PREFIX + std::to_string(i);
        int rc = zmq_connect(socket[i], conn_name.c_str());
        if (rc != 0) return rc;
    }
    return 0;
}

int close_zeromq_sockets(void **socket) {
    for (int i = 0; i < NZEROMQ; i++) {
        zmq_close(socket[i]);
    }
    return 0;
}

int send_zeromq(void *zeromq_socket, void *data, size_t data_size, int frame, int chunk) {
    nlohmann::json j;
    j["frame"] = frame;
    j["chunk"] = chunk;
    j["compression"] = "";
    if (writer_settings.compression == JF_COMPRESSION_BSHUF_ZSTD) j["compression"] = "bszstd";
    if (writer_settings.compression == JF_COMPRESSION_BSHUF_LZ4) j["compression"] = "bslz4";
    j["depth"] = experiment_settings.pixel_depth;
    j["images_per_file"] = writer_settings.images_per_file;
    j["prefix"] = writer_settings.HDF5_prefix;

    std::string s = j.dump();
    int rc = zmq_send (zeromq_socket, s.c_str(), s.length() - 1, ZMQ_SNDMORE);
    if (rc != 0) return rc;

    zmq_msg_t msg;
    rc = zmq_msg_init_data (&msg, data, data_size, my_free, NULL); 
    if (rc != 0) return rc;
    rc = zmq_msg_send(&msg, zeromq_socket, 0);
    if (rc != 0) return rc;
    rc = zmq_msg_close(&msg);
    if (rc != 0) return rc;
    return 0;
}

int recv_zeromq(void *socket, size_t &frame_number, size_t &chunk, std::string &prefix, size_t &images_per_file, size_t &depth, compression_t &compression, char *data) {

    zmq_msg_t msg;

    int rc = zmq_msg_init (&msg);
    if (rc != 0) return -1;
    int nbytes = zmq_msg_recv (&msg, socket, 0); 
    if (nbytes <= 0) return -1;
    if (zmq_msg_more (&msg) <= 0) return -1;
    zmq_msg_close (&msg);

    rc = zmq_msg_init (&msg);
    if (rc != 0) return -1;
    nbytes = zmq_msg_recv (&msg, socket, 0); 
    if (nbytes <= 0) return -1;
    if (zmq_msg_more (&msg) <= 0) return -1;
    zmq_msg_close (&msg);

    return 0;
}
