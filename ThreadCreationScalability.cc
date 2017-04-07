#include <stdlib.h>
#include <atomic>
#include "Cycles.h"
#include "Arachne.h"

#define CORE_OCCUPANCY 3

using PerfUtils::Cycles;

// These should be thread-local structures
struct TLSCounter {
    static std::atomic<uint64_t> globalCount;

	// Private to each structure
	uint64_t privateCount;

 	TLSCounter() : privateCount(0){ }
	~TLSCounter() {
		globalCount += privateCount;
	}
	void increment() {
		privateCount++;
	}
};

thread_local TLSCounter tlsCounter;

std::atomic<uint64_t> TLSCounter::globalCount;
std::atomic<bool> shutdown;

void creator() {
	if (Arachne::createThread(creator) == Arachne::NullThread) {
		if (!shutdown)
			abort();
		else
			return;
	}
	tlsCounter.increment();
}

void timeKeeper(int seconds) {
    Arachne::sleep(seconds * static_cast<uint64_t>(1E9));
	shutdown = true;
	Arachne::shutDown();
}

/**
 * Pass in fixed number of cores, duration to run for in seconds.
 */
int main(int argc, const char** argv) {
    if (argc < 3) {
        printf("Usage: ./ThreadCreationScalability <NumCores> <Duration_Seconds>\n");
        exit(1);
    }
    int numCores = atoi(argv[1]);
    int numSeconds = atoi(argv[2]);
    // Initialize the library
    Arachne::minNumCores = numCores;
    Arachne::maxNumCores = numCores;
    Arachne::init(&argc, argv);

	uint64_t startTime = Cycles::rdtsc();
    Arachne::createThread(timeKeeper, numSeconds);
	for (int i = 0; i < numCores * CORE_OCCUPANCY; i++)
		Arachne::createThread(creator);
    Arachne::waitForTermination();
	// This is a slight overestimation of running time since we are including
	// time to shutDown.
	double duration = Cycles::toSeconds(Cycles::rdtsc() - startTime);
	printf("Thread Creations Per Second: %lf\n", static_cast<double>(TLSCounter::globalCount.load()) / duration);
    return 0;
}
