#include <random>
#include <thread>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "Arachne.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "Stats.h"
#include "CoreArbiter/Logger.h"

using PerfUtils::Cycles;

namespace Arachne{
extern bool disableLoadEstimation;
}

using PerfUtils::TimeTrace;

// Support a maximum of 100 million entries.
#define MAX_ENTRIES (1 << 27)

uint64_t latencies[MAX_ENTRIES];

std::atomic<uint64_t> completions;
std::atomic<uint64_t> failures;

struct Interval {
    uint64_t timeToRun;

    // NB: This number is in nanoseconds, but the granularity of our cycle
    // measruements are in the 10's of ns, so differences of less than 10 ns
    // are not meaningful.
    uint64_t durationPerThread;
    double creationsPerSecond;
} *intervals;

size_t numIntervals;
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
    memset(latencies, 0, MAX_ENTRIES*sizeof(uint64_t));

    // Prevent scheduling onto this core, since threads scheduled to this core
    // will never get a chance to run.
	Arachne::makeExclusiveOnCore();

    // Initialize interval
    size_t currentInterval = 0;

    // Start with a DCFT-style implementation based on shared-memory for communication
    // If that becomes too expensive, then switch to a separate thread for commands.
    uint64_t cyclesPerThread = Cycles::fromNanoseconds(intervals[currentInterval].durationPerThread);

	std::random_device rd;
	std::mt19937 gen(rd());

    std::exponential_distribution<double> intervalGenerator(intervals[currentInterval].creationsPerSecond);
//    printf("%.10f %.10f\n", 1.0/creationsPerSecond, intervalGenerator(gen));
    uint64_t nextCycleTime = Cycles::rdtsc() +
		Cycles::fromSeconds(intervalGenerator(gen));

    uint64_t currentTime = Cycles::rdtsc();
    uint64_t nextIntervalTime = currentTime +
        Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);
    // DCFT loop
    for (;; currentTime = Cycles::rdtsc()) {
        if (nextCycleTime < currentTime) {
            nextCycleTime = currentTime +
                Cycles::fromSeconds(intervalGenerator(gen));
            if (Arachne::createThread(fixedWork, cyclesPerThread, currentTime) == Arachne::NullThread)
                failures++;
        }

        // Advance the interval
        if (nextIntervalTime < currentTime) {
            TimeTrace::record("Before change interval");
            currentInterval++;
            if (currentInterval == numIntervals) break;
            nextIntervalTime = currentTime +
                Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);
            cyclesPerThread =
                Cycles::fromNanoseconds(
                        intervals[currentInterval].durationPerThread);
            intervalGenerator.param(
                    std::exponential_distribution<double>::param_type(
                    intervals[currentInterval].creationsPerSecond));
            TimeTrace::record("After change interval");
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

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Please specify a configuration file!\n");
        exit(1);
    }
    // First argument specifies a configuration file with the following format
    // <count_of_rows>
    // <time_to_run_in_ns> <attempted_creations_per_second> <thread_duration_in_ns>
    FILE *specFile = fopen(argv[1], "r");
    char buffer[1024];
    fgets(buffer, 1024, specFile);
    sscanf(buffer, "%zu", &numIntervals);
    intervals = new Interval[numIntervals];
    for (size_t i = 0; i < numIntervals; i++) {
        fgets(buffer, 1024, specFile);
        sscanf(buffer, "%lu %lf %lu",
                &intervals[i].timeToRun,
                &intervals[i].creationsPerSecond,
                &intervals[i].durationPerThread);
    }
    fclose(specFile);


    // Catch intermittent errors
    installSignalHandler();
    CoreArbiter::Logger::setLogLevel(CoreArbiter::WARNING);
    Arachne::Logger::setLogLevel(Arachne::WARNING);
	Arachne::minNumCores = 2;
	Arachne::maxNumCores = 5;
    Arachne::setErrorStream(stdout);
    Arachne::init();
    Arachne::createThread(dispatch);
    Arachne::waitForTermination();

    TimeTrace::setOutputFileName("LoadScaling.log");
    TimeTrace::print();
    printf("Completions = %lu\nFailures = %lu\n", completions.load(), failures.load());

    double total = static_cast<double>(completions) + static_cast<double>(failures);
    printf("Thread creation failure rate = %lf\n", static_cast<double>(failures.load()) / total);
    printf("Total Attempted Creations    = %lf\n", total);

    // Output latency and throughput for each period.
    // Translate cycles to nanoseconds
//    for (size_t i = 0; i < completions; i++)
//        latencies[i] = Cycles::toNanoseconds(latencies[i]);
//    printf("Throughput = %lf requests / second \n", static_cast<double>(completions.load()) / experimentDurationInSeconds);
//    printStatistics("RequestCompletionLatency", latencies, completions, "data");
}
