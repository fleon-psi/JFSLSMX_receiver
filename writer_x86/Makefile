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
ifneq ("$(wildcard ../../../../ActionTypes.md)","")
SNAP_ROOT=$(abspath ../../../../)
else
$(info You are not building your software from the default directory (/path/to/snap/actions/<action_name>/sw) or specified a wrong $$SNAP_ROOT.)
$(error Please make sure that $$SNAP_ROOT is set up correctly.)
endif
endif

SNAP_LIBS=${SNAP_ROOT}/software/lib/libsnap.a -lcxl 
SNAP_INCLUDE=-I${SNAP_ROOT}/software/include -I/home/jungfrau/git/pslse/common

SLS_DETECTOR_PATH=/home/jungfrau/git/slsDetectorPackage
SLS_DETECTOR_INCLUDE=-I${SLS_DETECTOR_PATH}/slsDetectorSoftware/include -I${SLS_DETECTOR_PATH}/slsSupportLib/include
SLS_DETECTOR_LIB=-L${SLS_DETECTOR_PATH}/build/bin/ -lSlsDetector -lSlsSupport

HDF5_PATH=/opt/hdf5-1.10.5
HDF5_LIBS=$(HDF5_PATH)/lib/libhdf5.a

CXX=icpc
CC=icc
CFLAGS=-g -std=c99 -Ofast -g -static-intel -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info
CXXFLAGS= -std=c++11 -Ofast -g -static-intel -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info
LDFLAGS= -Ofast -g -static-intel -xHost -ip -llz4 -lm -lpthread -lz -libverbs -debug inline-debug-info -lssh
CPPFLAGS= -I. -I../include -I${HDF5_PATH}/include $(SLS_DETECTOR_INCLUDE) $(SNAP_INCLUDE)
EXECS=JFWriter

WR_SRCS=NetIO.o JFWriter.o FileIO.o WriterThread.o DetConfig.o sharedVariables.o ../IB_Transport.o ../bitshuffle/bshuf_h5filter.o  ../bitshuffle/bitshuffle.o ../bitshuffle/bitshuffle_core.o ../bitshuffle/iochain.o

all: JFWriter

JFWriter: $(WR_SRCS)
	$(CXX) $(WR_SRCS) -o JFWriter $(JF_LDLIBS) $(HDF5_LIBS) $(LDFLAGS) $(SLS_DETECTOR_LIB) -lzstd

clean:
	rm -f *.o ../*.o ../bitshuffle/*.o JFWriter
 

