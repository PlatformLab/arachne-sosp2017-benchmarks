CXX ?= g++

PERFUTILS=../PerfUtils
ARACHNE=../Arachne
COREARBITER=../CoreArbiter

BIN_DIR=bin

LIBS=-I$(ARACHNE)/include -I$(COREARBITER)/include  -L$(ARACHNE)/lib -lArachne  -L$(COREARBITER)/lib \
	-lCoreArbiter -I$(PERFUTILS)/include $(PERFUTILS)/lib/libPerfUtils.a -lpcrecpp -pthread

CXXFLAGS=-g -std=c++11 -O3 -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings -Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend -Woverloaded-virtual -Wcast-qual -Wcast-align -Wconversion -fomit-frame-pointer $(EXTRA_CXXFLAGS)

ARBITER_BENCHMARK_BINS = CoreRequest_Noncontended CoreRequest_Noncontended_Latency CoreRequest_Contended_Timeout CoreRequest_Contended_Timeout_Latency CoreRequest_Contended CoreRequest_Contended_Latency
UNIFIED_BENCHMARK_BINS = SyntheticWorkload ThreadCreationScalability VaryCoreIncreaseThreshold CoreAwareness UniformWorkload

all: $(ARBITER_BENCHMARK_BINS) $(UNIFIED_BENCHMARK_BINS)

ExtractStats:

$(ARBITER_BENCHMARK_BINS) : % : %.cc $(COREARBITER)/lib/libCoreArbiter.a
	$(CXX)  $(DEBUG) $(CXXFLAGS)  $^ $(LIBS) -o $@

$(UNIFIED_BENCHMARK_BINS): % : %.cc $(ARACHNE)/lib/libArachne.a $(COREARBITER)/lib/libCoreArbiter.a
	$(CXX)  $(DEBUG) $(CXXFLAGS)  $^ $(LIBS) -o $@

install: $(ARBITER_BENCHMARK_BINS) $(UNIFIED_BENCHMARK_BINS)
	mkdir -p $(BIN_DIR)
	cp -f $(ARBITER_BENCHMARK_BINS) $(UNIFIED_BENCHMARK_BINS) $(BIN_DIR)

clean:
	rm -f $(ARBITER_BENCHMARK_BINS) $(UNIFIED_BENCHMARK_BINS) *.log
