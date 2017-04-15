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
using Arachne::PerfStats;

namespace Arachne{
extern bool disableLoadEstimation;
extern double maxIdleCoreFraction;
extern double loadFactorThreshold;
}

using PerfUtils::TimeTrace;

// Support a maximum of 100 million entries.
#define MAX_ENTRIES (1 << 27)

uint64_t latencies[MAX_ENTRIES];

std::atomic<uint64_t> arrayIndex;

struct Interval {
    uint64_t timeToRun;

    // NB: This number is in nanoseconds, but the granularity of our cycle
    // measurements are in the 10's of ns, so differences of less than 10 ns
    // are not meaningful.
    uint64_t durationPerThread;
    int numCoresOfLoad;
} *intervals;

size_t numIntervals;

// The values of arrayIndex before each change in load, which we can use later
// to compute latency and throughput. Note that this value and perfStats
// are not read atomically, but we believe the difference should be
// negligible.
std::vector<uint64_t> indices;
// The performance statistics before each change in load, which we can use
// later to compute utilization.
std::vector<PerfStats> perfStats;

// This represents the current number of threads worth of load.
std::atomic<int> curNumThreads;

/**
  * Spin for duration cycles, and then compute latency from creation time.
  */
void fixedWork(int threadId, uint64_t duration, uint64_t creationTime) {
    if (threadId >= curNumThreads) {
        TimeTrace::record("Thread Id %d dropping off", threadId);
        return;
    }
    // Do some fixed amount of work.
    uint64_t stop = Cycles::rdtsc() + duration;
    while (Cycles::rdtsc() < stop);

    // Create a child thread. May have to retry because thread creation during
    // scale-down can cause a failure.
    while (Arachne::createThread(fixedWork, threadId, duration, stop) == Arachne::NullThread);

    // Compute latency
    uint64_t latency = Cycles::rdtsc() - creationTime;
    latencies[arrayIndex++] = latency;
}

void dispatch() {
    // Page in our data store
    memset(latencies, 0, MAX_ENTRIES*sizeof(uint64_t));

    // Prevent scheduling onto this core, since threads scheduled to this core
    // will never get a chance to run.
	Arachne::makeExclusiveOnCore();

    // Initialize interval
    size_t currentInterval = 0;

    uint64_t cyclesPerThread = Cycles::fromNanoseconds(intervals[currentInterval].durationPerThread);
    int numCoresOfLoad = intervals[currentInterval].numCoresOfLoad;


    PerfStats stats;
    PerfStats::collectStats(&stats);

    indices.push_back(arrayIndex);
    perfStats.push_back(stats);

    uint64_t currentTime = Cycles::rdtsc();
    uint64_t nextIntervalTime = currentTime +
        Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);

    // Initialize the threads here.
    curNumThreads = numCoresOfLoad;
    for (int i = 0; i < numCoresOfLoad; i++) {
        TimeTrace::record("Creating thread with Id = %d\n", i);
        Arachne::createThread(fixedWork, i, cyclesPerThread, Cycles::rdtsc());
    }

    // DCFT loop
    for (;; currentTime = Cycles::rdtsc()) {
        if (nextIntervalTime < currentTime) {
            // Collect latency, throughput, and core utilization information from the past interval
            PerfStats::collectStats(&stats);
            indices.push_back(arrayIndex);
            perfStats.push_back(stats);

            // Advance the interval
            currentInterval++;
            if (currentInterval == numIntervals) break;
            TimeTrace::record("Load Change START %u --> %u Cores.",
                    static_cast<uint32_t>(intervals[currentInterval - 1].numCoresOfLoad),
                    static_cast<uint32_t>(intervals[currentInterval].numCoresOfLoad));

            nextIntervalTime = currentTime +
                Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);
            cyclesPerThread =
                Cycles::fromNanoseconds(
                        intervals[currentInterval].durationPerThread);
            numCoresOfLoad = intervals[currentInterval].numCoresOfLoad;

            if (numCoresOfLoad < curNumThreads) {
                // Scale down the number of threads.
                curNumThreads = numCoresOfLoad;
            } else if (numCoresOfLoad > curNumThreads) {
                // Scale up the the number of cores.
                int formerCores = curNumThreads;
                curNumThreads = numCoresOfLoad;
                for (int i = 0; i < numCoresOfLoad - formerCores; i++) {
                    TimeTrace::record("Creating thread with Id = %d\n", i + formerCores);
                    Arachne::createThread(fixedWork, i + formerCores,
                            cyclesPerThread, currentTime);
                }
            }

            TimeTrace::record("Load Change END %u --> %u Cores.",
                    static_cast<uint32_t>(intervals[currentInterval - 1].numCoresOfLoad),
                    static_cast<uint32_t>(intervals[currentInterval].numCoresOfLoad));

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

int main(int argc, const char** argv) {
    CoreArbiter::Logger::setLogLevel(CoreArbiter::WARNING);
    Arachne::Logger::setLogLevel(Arachne::DEBUG);

    if (argc < 2) {
        printf("Please specify a configuration file!\n");
        exit(1);
    }

	Arachne::minNumCores = 2;
	Arachne::maxNumCores = 5;
    Arachne::setErrorStream(stderr);
    Arachne::init(&argc, argv);

    // First argument specifies a configuration file with the following format
    // <count_of_rows>
    // <time_to_run_in_ns> <number_of_cores_of_load> <thread_duration_in_ns>
    FILE *specFile = fopen(argv[1], "r");
    if (!specFile) {
        printf("Configuration file '%s' non existent!\n", argv[1]);
        exit(1);
    }
    char buffer[1024];
    fgets(buffer, 1024, specFile);
    sscanf(buffer, "%zu", &numIntervals);
    intervals = new Interval[numIntervals];
    for (size_t i = 0; i < numIntervals; i++) {
        fgets(buffer, 1024, specFile);
        sscanf(buffer, "%lu %d %lu",
                &intervals[i].timeToRun,
                &intervals[i].numCoresOfLoad,
                &intervals[i].durationPerThread);
    }
    fclose(specFile);

    if (argc > 2) {
        Arachne::maxIdleCoreFraction = atof(argv[2]);
        Arachne::loadFactorThreshold =  atof(argv[2]);
    }

    // Catch intermittent errors
    installSignalHandler();
    Arachne::createThread(dispatch);
    Arachne::waitForTermination();

    // Output TimeTrace for human reading
    size_t index = rindex(argv[1], static_cast<int>('.')) - argv[1];
    char outTraceFileName[1024];
    strncpy(outTraceFileName, argv[1], index);
    outTraceFileName[index] = '\0';
    strncat(outTraceFileName, ".log", 4);
    TimeTrace::setOutputFileName(outTraceFileName);
    TimeTrace::keepOldEvents = true;
    TimeTrace::print();

    // Sanity check
    if (arrayIndex >= MAX_ENTRIES) {
        puts("Guaranteed memory corruption.");
        abort();
    }

    // Convert latencies to ns
    for (size_t i = 0; i < arrayIndex; i++)
        latencies[i] = Cycles::toNanoseconds(latencies[i]);
    // Output core utilization, median & 99% latency, and throughput for each interval in a
    // plottable format.
    puts("Duration,Offered Load,Core Utilization,50\% Latency,90\%,99\%,Max,Throughput,Load Factor,Core++,Core--,U x LF,(1-idle) x LF");
    for (size_t i = 1; i < indices.size(); i++) {
        double durationOfInterval = Cycles::toSeconds(perfStats[i].collectionTime -
            perfStats[i-1].collectionTime);
        uint64_t idleCycles = perfStats[i].idleCycles - perfStats[i-1].idleCycles;
        uint64_t totalCycles = perfStats[i].totalCycles - perfStats[i-1].totalCycles;
        double utilization = static_cast<double>(totalCycles - idleCycles) /
            static_cast<double>(totalCycles);

        // Note that this is completed tasks per second, where each task is currently 2 us
        uint64_t throughput = static_cast<uint64_t>(
                static_cast<double>(indices[i] - indices[i-1]) / durationOfInterval);

        // Compute load factor.
        uint64_t weightedLoadedCycles = perfStats[i].weightedLoadedCycles - perfStats[i-1].weightedLoadedCycles;
        double loadFactor = static_cast<double>(weightedLoadedCycles) / static_cast<double>(totalCycles);

        // Subtract the exclusive dispatch core.
        uint64_t numSharedCores = perfStats[i].numCoreIncrements - perfStats[i].numCoreDecrements - 1;
        double idleCoreFraction = static_cast<double>(idleCycles) / static_cast<double>(totalCycles);
        double totalIdleCores = idleCoreFraction * static_cast<double>(numSharedCores);

        // Compute core count changes
        uint64_t numIncrements = perfStats[i].numCoreIncrements - perfStats[i-1].numCoreIncrements;
        uint64_t numDecrements = perfStats[i].numCoreDecrements - perfStats[i-1].numCoreDecrements;

        // Median and 99% Latency
        // Note that this computation will modify data
        Statistics mathStats = computeStatistics(latencies + indices[i-1], indices[i] - indices[i-1]);
        printf("%lf,%d,%lf,%lu,%lu,%lu,%lu,%lu,%lf,%lu,%lu,%lf,%lf\n", durationOfInterval,
                intervals[i-1].numCoresOfLoad, utilization,
                mathStats.median, mathStats.P90, mathStats.P99, mathStats.max,
                throughput, loadFactor, numIncrements, numDecrements,
                utilization * loadFactor, (1-totalIdleCores)*loadFactor);
    }

    // Output times at which cores changed, relative to the start time.
}
