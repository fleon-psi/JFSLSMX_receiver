# JF SLS MX Receiver

Application set to receive data from the JUNGFRAU detector at Swiss Light Source macromolecular crystallography beamlines.

Ackonwledgements: A. Mozzanica (PSI), M. Bruckner (PSI), C. Lopez-Cuenca (PSI), M. Wang (PSI), L. Sala (PSI), A. Castellane (IBM), B. Mesnet (IBM), K. Diederichs (Uni Konstanz)

### Hardware requirements
1. JUNGFRAU detector (optimally 4M with 2 kHz enabled read-out boards)
2. IC 922 server equipped with AD9H3 and Tesla T4 boards
3. x86 server connected to IC 922 with Mellanox Infiniband
4. 100G fiber-optic switch

### Contents
1. `hw` - FPGA design of SNAP/OC-Accel action
2. `receiver_p9` - POWER9 code to interface with SNAP/OC-Accel action and with GPU
2. `writer_x86` - x86 application to receive frames received by POWER9, performs compression and HDF5 writing - it also controls detector - communication via REST interface on port 5232
3. `plugin_xds_x86` - plugin to analyze images written by `writer_x86` by XDS (due to splitting of one frame to mutliple chunks, this is impossible with current plugins)
4. `fronend_UI` - frontend UI written in React JavaScript framework

### Dependencies for Intel server
1. HDF5 library (tested with v.1.10.6)
2. Pistache REST server for C++ (submodule)
3. JSON library for C++ (submodule)
4. PSI SLS Detector package (submodule)
5. Zstandard compression library (submodule)
6. OpenCV library for preview (tested with v. 4.3.0)
7. OC-Accel or SNAP 
8. IB Verbs library (tested with OFED v. 5)

Requires C++14 compiler and libraries. Tested with Intel Compiler v.2020. On RHEL 7 requires to install devtools with newer libstdc++ (tested with devtools-8 and devtools-9).

For logging, InfluxDB server is required. For user interface, a web server (e.g. Apache httpd) is necessary + node.js is needed to compiler frontend user interface.
### Dependencies for POWER9 server
1. CUDA
2. IB Verbs library (tested with OFED v. 5)

Testes with IBM XL C/C++ compiler.
