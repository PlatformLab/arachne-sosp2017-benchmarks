LIBS=-ICoreArbiter CoreArbiter/libCoreArbiter.a -IPerfUtils PerfUtils/libPerfUtils.a -pthread
CXXFLAGS=-std=c++11 -O3 -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings -Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend -Woverloaded-virtual -Wcast-qual -Wcast-align -Wconversion -fomit-frame-pointer

ARBITER_BENCHMARK_BINS = CoreRequest_Noncontended

all: $(ARBITER_BENCHMARK_BINS)

$(ARBITER_BENCHMARK_BINS) : % : %.cc CoreArbiter/libCoreArbiter.a
	g++  $(DEBUG) $(CXXFLAGS)  $< $(LIBS) -o $@

clean:
	rm -f $(ARBITER_BENCHMARK_BINS) *.log
