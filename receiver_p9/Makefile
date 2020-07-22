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

SNAP_ROOT=/home/jungfrau/git/oc-accel
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

SNAP_LIBS=${SNAP_ROOT}/software/lib/libosnap.a -locxl 
SNAP_INCLUDE=-I${SNAP_ROOT}/software/include #-I/home/jungfrau/git/pslse/common

CXX=xlC_r
CC=xlc_r
CFLAGS= -std=c99 -qipa -O5 -Wall -mcpu=power9 -qarch=pwr9
CXXFLAGS= -std=c++11 -qipa -O5 -g  -Wall -mcpu=power9 -qarch=pwr9
LDFLAGS= -qipa -O5 -mcpu=power9 -qarch=pwr9 -llz4 -lm -lpthread -lz -libverbs

CUDA_PATH=/usr/local/cuda-10.2/
CUDA_LIBS=$(CUDA_PATH)/lib64/libcudart_static.a -lrt

CPPFLAGS= -I. -I../include ${SNAP_INCLUDE}

RCV_SRCS=analyze_spots.o JFReceiver.o sharedVariables.o SendThread.o ../common/IB_Transport.o SnapThread.o find_spots.o

all: JFReceiver

find_spots.o: find_spots.cu
	$(CUDA_PATH)/bin/nvcc  -gencode=arch=compute_75,code=compute_75 -O3 $< -c $@ -ccbin xlC $(CPPFLAGS)

JFReceiver: $(RCV_SRCS)
	$(CXX) $(RCV_SRCS) -o JFReceiver $(JF_LDLIBS) $(LDFLAGS) $(SNAP_LIBS) $(CUDA_LIBS)

clean:
	rm -f *.o ../*.o JFReceiver
 
