#include <random>
#include <thread>
#include "Arachne.h"
#include "PerfUtils/Cycles.h"

using PerfUtils::Cycles;

std::atomic<uint64_t> completions;
void fixedWork(uint64_t duration) {
	Cycles::sleep(duration);
    completions++;
}

void dispatch() {
	Arachne::makeExclusiveOnCore();
    // Start with a DCFT-style implementation based on shared-memory for communication
    // If that becomes too expensive, then switch to a separate thread for commands.

    // Note that this controlling thread will necessarily burn up an entire core, since
    // it has to poll to see whether it should issue more requests as well as
    // polling on shared memory for commands.

    // TODO: Use shared memory to expose these controls.

    // Could it be that the right way to do this is to simply provide Arachne
    // with the equivalent of Go's LockOSThread? To get an exclusive core for
    // this function alone? That way, Arachne can work with such threads and
    // schedule other threads as needed.
    // Generate time in ns because that's more intuitive for users and 
	// Must be floating point type...
	double creationsPerSecond = 1000000U;
	uint64_t durationPerThread = 0;

	std::random_device rd;
	std::mt19937 gen(rd());

    std::exponential_distribution<double> intervalGenerator(creationsPerSecond);
    printf("%.10f %.10f\n", 1.0/creationsPerSecond, intervalGenerator(gen));
    uint64_t nextCycleTime = Cycles::rdtsc() +
		Cycles::fromSeconds(intervalGenerator(gen));

    uint64_t currentTime = Cycles::rdtsc();
    uint64_t finalTime = currentTime + Cycles::fromSeconds(1);
    // DCFT loop
    uint64_t failureRate = 0;
    for (; currentTime < finalTime ; currentTime = Cycles::rdtsc()) {
        if (nextCycleTime < currentTime) {
            nextCycleTime = currentTime +
                Cycles::fromSeconds(intervalGenerator(gen));
            if (Arachne::createThread(fixedWork, durationPerThread) == Arachne::NullThread)
                failureRate++;
        }
    }
    printf("Completions %lu\n"
           "Failed Creations %lu\n", completions.load(), failureRate);
    fflush(stdout);
    Arachne::makeSharedOnCore();
    Arachne::shutDown();
}

/**
 * This synthetic benchmarking tool allows us to create threads at a Poisson
 * arrival rate with a configurable mean. These threads run for a configurable
 * amount of time. We record the times that configuration changes go into
 * effect, so we can plot them later against the effects that these changes
 * caused.
 *
 * The inter-arrival time of a Poisson distribution with mean λ is given
 * by an exponential distribution with mean (1/λ), so we use an exponential
 * distribution from the c++ standard library.
 *
 * Note that we should probably bechmark the cost of extracting randomness as
 * well, but we haven't yet done that.
 */

int main() {
	Arachne::minNumCores = 2;
	Arachne::maxNumCores = 4;
    Arachne::init();
    Arachne::createThread(dispatch);
    Arachne::waitForTermination();
}
