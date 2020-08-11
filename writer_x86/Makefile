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

SLS_DETECTOR_PATH=/home/jungfrau/git/slsDetectorPackage
SLS_DETECTOR_INCLUDE=-I${SLS_DETECTOR_PATH}/slsDetectorSoftware/include -I${SLS_DETECTOR_PATH}/slsSupportLib/include
SLS_DETECTOR_LIB=-L${SLS_DETECTOR_PATH}/build/bin/ -lSlsDetector -lSlsSupport

HDF5_PATH=/opt/hdf5-1.10.6
HDF5_LIBS=$(HDF5_PATH)/lib/libhdf5.a

PISTACHE_PATH=../pistache/
PISTACHE_LIB=$(PISTACHE_PATH)/build/src/libpistache.a

OPENCV_PATH=/usr/local/
OPENCV_INCLUDE=-I/usr/local/include/opencv4
OPENCV_LIB=-L$(OPENCV_PATH) -lopencv_imgcodecs -lopencv_imgproc -lopencv_core

CXX=icpc
CC=icc
CFLAGS=-g -std=c99 -Ofast -g -static-intel -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info -fPIC
CXXFLAGS= -std=c++14 -Ofast -g -static-intel -xHost -ip -Wall -DUSE_ZSTD -debug inline-debug-info
LDFLAGS= -Ofast -g -static-intel -xHost -ip -lm -lpthread -lz -libverbs -debug inline-debug-info -lssh $(IPPROOT)/lib/intel64/libippdc.a $(IPPROOT)/lib/intel64/libipps.a $(IPPROOT)/lib/intel64/libippcore.a
CPPFLAGS= -I. -I../include -I../lz4 -I../zstd/lib -I${HDF5_PATH}/include -I$(PISTACHE_PATH)/include $(SLS_DETECTOR_INCLUDE) -I/usr/local/include/opencv4/

WR_SRCS=ParameterIO.o Preview.o JFWriter.o NetIO.o FileIO.o WriterThread.o DetConfig.o sharedVariables.o MetadataThread.o LogInfluxDB.o ../common/IB_Transport.o ../common/Coord.o ../bitshuffle/bshuf_h5filter.o  ../bitshuffle/bitshuffle.o ../bitshuffle/bitshuffle_core.o ../bitshuffle/iochain.o ../lz4/lz4.c

all: RESTserver

RESTserver: $(WR_SRCS) RESTserver.o
	$(CXX) $(WR_SRCS) RESTserver.o -o RESTserver $(JF_LDLIBS) $(HDF5_LIBS) $(LDFLAGS) $(SLS_DETECTOR_LIB) $(PISTACHE_LIB) $(OPENCV_LIB) ../zstd/lib/libzstd.a

clean:
	rm -f *.o ../*.o ../bitshuffle/*.o JFWriter
 

