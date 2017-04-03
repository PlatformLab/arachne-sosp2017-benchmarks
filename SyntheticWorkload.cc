#include <random>
#include <thread>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "Arachne.h"
#include "PerfUtils/Cycles.h"
#include "CoreArbiter/Logger.h"
#include "Stats.h"

using PerfUtils::Cycles;
namespace Arachne{
extern bool disableLoadEstimation;
}

// Support a maximum of 100 million entries.
#define MAX_ENTRIES 1 << 27

uint64_t latencies[MAX_ENTRIES];

// TODO: Use shared memory to expose these controls.
double experimentDurationInSeconds = 5;

// Number of creations per second
double creationsPerSecond = 1000000U;

// NB: This number is in nanoseconds, but the granularity of our cycle
// measruements are in the 10's of ns, so differences of less than 10 ns
// are not meaningful.
uint64_t durationPerThread = 2000;

std::atomic<uint64_t> completions;
std::atomic<uint64_t> failureRate;

/**
  * Spin for duration cycles, and then compute latency from creation time.
  */
void fixedWork(uint64_t duration, uint64_t creationTime) {
    uint64_t stop = Cycles::rdtsc() + duration;
    while (Cycles::rdtsc() < stop);
    uint64_t latency = Cycles::rdtsc() - creationTime;
    latencies[completions++] = latency;
}

void dispatch() {
    // Page in our data store
    memset(latencies, 0, MAX_ENTRIES);

    // Prevent schedulign onto this core, since threads scheduled to this core
    // will never get a chance to run.
	Arachne::makeExclusiveOnCore();

    // Start with a DCFT-style implementation based on shared-memory for communication
    // If that becomes too expensive, then switch to a separate thread for commands.

    // Note that this controlling thread will necessarily burn up an entire core, since
    // it has to poll to see whether it should issue more requests as well as
    // polling on shared memory for commands.

    uint64_t cyclesPerThread = Cycles::fromNanoseconds(durationPerThread);


	std::random_device rd;
	std::mt19937 gen(rd());

    std::exponential_distribution<double> intervalGenerator(creationsPerSecond);
    printf("%.10f %.10f\n", 1.0/creationsPerSecond, intervalGenerator(gen));
    uint64_t nextCycleTime = Cycles::rdtsc() +
		Cycles::fromSeconds(intervalGenerator(gen));

    uint64_t currentTime = Cycles::rdtsc();
    uint64_t finalTime = currentTime + Cycles::fromSeconds(experimentDurationInSeconds);
    // DCFT loop
    for (; currentTime < finalTime ; currentTime = Cycles::rdtsc()) {
        if (nextCycleTime < currentTime) {
            nextCycleTime = currentTime +
                Cycles::fromSeconds(intervalGenerator(gen));
            if (Arachne::createThread(fixedWork, cyclesPerThread, currentTime) == Arachne::NullThread)
                failureRate++;
        }
    }
    // Shutdown immediately to avoid overcounting.
    Arachne::makeSharedOnCore();
    Arachne::shutDown();
}

/**
  * This method attempts to attach gdb to the currently running process.
  */
void invokeGDB(int signum) {
    char buf[256];
    snprintf(buf, sizeof(buf), "/usr/bin/gdb -p %d",  getpid());
    int ret = system(buf);

    if (ret == -1) {
        fprintf(stderr, "Failed to attach gdb upon receiving the signal %s\n",
                strsignal(signum));
    }
}

void
signalHandler(int signum) {
    // Prevent repeated invocations
    struct sigaction signalAction;
    signalAction.sa_handler = SIG_DFL;
    signalAction.sa_flags = SA_RESTART;
    sigaction(signum, &signalAction, NULL);

    // Process the signal
    invokeGDB(signum);
}

void
installSignalHandler() {
    struct sigaction signalAction;
    signalAction.sa_handler = signalHandler;
    signalAction.sa_flags = SA_RESTART;
    if (sigaction(SIGSEGV, &signalAction, NULL) != 0)
        fprintf(stderr, "Couldn't set signal handler for SIGSEGV");
    if (sigaction(SIGABRT, &signalAction, NULL) != 0)
        fprintf(stderr, "Couldn't set signal handler for SIGABRT");
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
    // Catch intermittent errors
    installSignalHandler();
    CoreArbiter::Logger::setLogLevel(CoreArbiter::WARNING);
    Arachne::Logger::setLogLevel(Arachne::NOTICE);
	Arachne::minNumCores = 2;
	Arachne::maxNumCores = 4;
    Arachne::init();
    Arachne::createThread(dispatch);
    Arachne::waitForTermination();


    // Output latency and throughput
    // Translate cycles to nanoseconds
    for (size_t i = 0; i < completions; i++)
        latencies[i] = Cycles::toNanoseconds(latencies[i]);
    printf("Throughput = %lf requests / second \n", static_cast<double>(completions.load()) / experimentDurationInSeconds);
    printStatistics("RequestCompletionLatency", latencies, completions, "data");
}
