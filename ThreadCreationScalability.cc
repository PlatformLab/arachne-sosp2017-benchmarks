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

// Both of these times are in cycles
std::atomic<uint64_t> stopTime;
std::atomic<uint64_t> startTime;

// This time is in seconds
std::atomic<double> duration;

Arachne::Semaphore done;

void creator() {
    if (Cycles::rdtsc() < stopTime) {
        if (Arachne::createThread(creator) == Arachne::NullThread) {
			abort();
        }
        tlsCounter.increment();
    } else {
        done.notify();
    }
}

void timeKeeper(int numCores, int seconds) {
	startTime = Cycles::rdtsc();
    uint64_t durationInCycles = Cycles::fromSeconds(seconds);
    stopTime = Cycles::rdtsc() + durationInCycles;
	for (int i = 0; i < numCores * CORE_OCCUPANCY; i++)
		Arachne::createThread(creator);
    // Wait for all threads to finish, using a semaphor
	for (int i = 0; i < numCores * CORE_OCCUPANCY; i++)
        done.wait();

	duration = Cycles::toSeconds(Cycles::rdtsc() - startTime);
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

    Arachne::createThread(timeKeeper, numCores, numSeconds);
    Arachne::waitForTermination();
	printf("Thread Creations Per Second: %lf\n", static_cast<double>(TLSCounter::globalCount.load()) / duration);
    return 0;
}
