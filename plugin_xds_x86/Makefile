#
# Copyright 2017 International Business Machines
# Copyright 2020 Paul Scherrer Institute
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

HOSTNAME := $(shell hostname)
UNAME_P := $(shell uname -p)

HDF5_PATH=/opt/hdf5-1.10.6
HDF5_LIBS=-L$(HDF5_PATH)/lib -lhdf5
## If HDF5 (at least 1.10.3) and Zstd are not provided by OS, it is better
## to include these as static libraries, so they are bundled with the plugin.
## This rerquires adding -fPIC to CFLAGS and CXXFLAGS for both compilations
#HDF5_LIBS=$(HDF5_PATH)/lib/libhdf5.a

CXX=icpc
CC=icc
CFLAGS=-g -std=c99 -static-intel -Ofast -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info -fPIC -DWITH_IPP
CXXFLAGS= -std=c++14 -static-intel -Ofast -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info -fPIC -DOFFLINE -DWITH_IPP
LDFLAGS= -static-intel -Ofast -xHost -ip -debug inline-debug-info -Wl,--as-needed -lippdc -lipps -lippcore
CPPFLAGS= -I. -I../include -I../lz4 -I${HDF5_PATH}/include

PLUGIN_SRCS=plugin.o ../bitshuffle/bshuf_h5filter.o  ../bitshuffle/bitshuffle.o ../bitshuffle/bitshuffle_core.o ../bitshuffle/iochain.o ../lz4/lz4.o

all: psi-jungfrau-plugin.so

psi-jungfrau-plugin.so: $(PLUGIN_SRCS)
	$(CXX) $(PLUGIN_SRCS) -o psi-jungfrau-plugin.so -shared $(HDF5_LIBS) $(LDFLAGS) -lzstd

clean:
	rm -f *.o ../*.o ../bitshuffle/*.o ../lz4/lz4.o psi-jungfrau-plugin.so
 

