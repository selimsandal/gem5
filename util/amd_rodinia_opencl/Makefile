ROCM_PATH ?= /opt/rocm
CXX ?= clang++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
CPPFLAGS += -I$(ROCM_PATH)/include
LDFLAGS += -L$(ROCM_PATH)/lib -Wl,-rpath,$(ROCM_PATH)/lib
LDLIBS += -lOpenCL

all: amd_rodinia_opencl

amd_rodinia_opencl: amd_rodinia_opencl.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f amd_rodinia_opencl

.PHONY: all clean
