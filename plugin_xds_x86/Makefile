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

# Finding $SNAP_ROOT
ifndef SNAP_ROOT
# check if we are in sw folder of an action (three directories below snap root)
ifneq ("$(wildcard ../../../ActionTypes.md)","")
SNAP_ROOT=$(abspath ../../../)
else
$(info You are not building your software from the default directory (/path/to/snap/actions/<action_name>/sw) or specified a wrong $$SNAP_ROOT.)
$(error Please make sure that $$SNAP_ROOT is set up correctly.)
endif
endif

SNAP_LIBS=${SNAP_ROOT}/software/lib/libsnap.a -lcxl 
SNAP_INCLUDE=-I${SNAP_ROOT}/software/include

HDF5_PATH=/opt/hdf5-1.10.5
HDF5_LIBS=-L$(HDF5_PATH)/lib -lhdf5

CXX=icpc
CC=icc
CFLAGS=-g -std=c99 -static-intel -Ofast -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info -DOFFLINE -fPIC -DWITH_IPP
CXXFLAGS= -std=c++14 -static-intel -Ofast -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info -DOFFLINE -fPIC -DOFFLINE -DWITH_IPP
LDFLAGS= -static-intel -Ofast -xHost -ip -debug inline-debug-info -Wl,--as-needed -lippdc -lipps -lippcore
CPPFLAGS= -I. -I../include -I${HDF5_PATH}/include

PLUGIN_SRCS=plugin.o ../bitshuffle/bshuf_h5filter.o  ../bitshuffle/bitshuffle.o ../bitshuffle/bitshuffle_core.o ../bitshuffle/iochain.o ../lz4/lz4.o

all: psi-jungfrau-plugin.so

psi-jungfrau-plugin.so: $(PLUGIN_SRCS)
	$(CXX) $(PLUGIN_SRCS) -o psi-jungfrau-plugin.so -shared $(HDF5_LIBS) $(LDFLAGS) -lzstd

clean:
	rm -f *.o ../*.o ../bitshuffle/*.o ../lz4/lz4.o psi-jungfrau-plugin.so
 

