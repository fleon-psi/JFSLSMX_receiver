# JF SLS MX Receiver

Application set to receive data from the JUNGFRAU detector at Swiss Light Source macromolecular crystallography beamlines.

Ackonwledgements: A. Mozzanica (PSI), M. Bruckner (PSI), C. Lopez-Cuenca (PSI), M. Wang (PSI), L. Sala (PSI), A. Castellane (IBM), B. Mesnet (IBM), K. Diederichs (Uni Konstanz)

Requires:
1. JUNGFRAU detector (optimally 4M with 2 kHz enabled read-out boards)
2. IC 922 server equipped with AD9H3 and Tesla T4 boards
3. x86 server connected to IC 922 with Mellanox Infiniband
4. 100G fiber-optic switch

Contents:
1. `hw` - FPGA design of SNAP/OC-Accel action
2. `receiver_p9` - POWER9 code to interface with SNAP/OC-Accel action and with GPU
2. `writer_x86` - x86 application to receive frames received by POWER9, performs compression and HDF5 writing - it also controls detector - communication via REST interface on port 5232
3. `plugin_xds_x86` - plugin to analyze images written by `writer_x86` by XDS (due to splitting of one frame to mutliple chunks, this is impossible with current plugins)

Dependencies:
1. HDF5 library (tested with 1.10.5)
2. Pistache REST server for C++ (submodule)
3. JSON library for C++ (submodule)
4. PSI SLS Detector package
5. OC-Accel or SNAP
6. CUDA
