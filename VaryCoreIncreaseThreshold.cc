#include <random>
#include <thread>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "Arachne/Arachne.h"
#include "Arachne/DefaultCorePolicy.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Stats.h"
#include "CoreArbiter/Logger.h"

using PerfUtils::Cycles;
using Arachne::PerfStats;
using PerfUtils::TimeTrace;

// Support a maximum of 100 million entries.
#define MAX_ENTRIES (1 << 27)

uint64_t latencies[MAX_ENTRIES];

std::atomic<uint64_t> arrayIndex;
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

// The values of arrayIndex before each change in load, which we can use later
// to compute latency and throughput. Note that this value and perfStats
// are not read atomically, but we believe the difference should be
// negligible.
std::vector<uint64_t> indices;
// The performance statistics before each change in load, which we can use
// later to compute utilization.
std::vector<PerfStats> perfStats;

/**
  * Spin for duration cycles, and then compute latency from creation time.
  */
void fixedWork(uint64_t duration, uint64_t creationTime) {
    uint64_t stop = Cycles::rdtsc() + duration;
    while (Cycles::rdtsc() < stop);
    uint64_t latency = Cycles::rdtsc() - creationTime;
    latencies[arrayIndex++] = latency;
}

void dispatch() {
    // Page in our data store
    memset(latencies, 0, MAX_ENTRIES*sizeof(uint64_t));

    // Initialize interval
    size_t currentInterval = 0;

    // Start with a DCFT-style implementation based on shared-memory for communication
    // If that becomes too expensive, then switch to a separate thread for commands.
    uint64_t cyclesPerThread = Cycles::fromNanoseconds(intervals[currentInterval].durationPerThread);

	std::random_device rd;
	std::mt19937 gen(rd());

    std::exponential_distribution<double> intervalGenerator(intervals[currentInterval].creationsPerSecond);
    uint64_t nextCycleTime = Cycles::rdtsc() +
		Cycles::fromSeconds(intervalGenerator(gen));


    PerfStats stats;
    PerfStats::collectStats(&stats);

    indices.push_back(arrayIndex);
    perfStats.push_back(stats);

    uint64_t currentTime = Cycles::rdtsc();
    uint64_t nextIntervalTime = currentTime +
        Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);

    TimeTrace::record("Beginning of benchmark");

    // DCFT loop
    for (;; currentTime = Cycles::rdtsc()) {
        if (nextCycleTime < currentTime) {
            // Keep trying to create this thread until we succeed.
            while (Arachne::createThread(fixedWork, cyclesPerThread, currentTime) == Arachne::NullThread);

            // Compute the next cycle time only after we win successfully
            nextCycleTime = currentTime +
                Cycles::fromSeconds(intervalGenerator(gen));
        }

        if (nextIntervalTime < currentTime) {
            // Collect latency, throughput, and core utilization information from the past interval
            PerfStats::collectStats(&stats);
            indices.push_back(arrayIndex);
            perfStats.push_back(stats);

            // Advance the interval
            currentInterval++;
            if (currentInterval == numIntervals) break;
            TimeTrace::record("Load Change START %u --> %u Creations Per Second.",
                    static_cast<uint32_t>(intervals[currentInterval - 1].creationsPerSecond),
                    static_cast<uint32_t>(intervals[currentInterval].creationsPerSecond));

            nextIntervalTime = currentTime +
                Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);
            cyclesPerThread =
                Cycles::fromNanoseconds(
                        intervals[currentInterval].durationPerThread);
            intervalGenerator.param(
                    std::exponential_distribution<double>::param_type(
                    intervals[currentInterval].creationsPerSecond));
            TimeTrace::record("Load Change END %u --> %u Creations Per Second.",
                    static_cast<uint32_t>(intervals[currentInterval - 1].creationsPerSecond),
                    static_cast<uint32_t>(intervals[currentInterval].creationsPerSecond));

        }
    }
    // Shutdown immediately to avoid overcounting completions.
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
    Arachne::Logger::setLogLevel(Arachne::WARNING);

    // Catch intermittent errors
    installSignalHandler();

    if (argc < 2) {
        printf("Please specify a configuration file!\n");
        exit(1);
    }

	Arachne::minNumCores = 2;
	Arachne::maxNumCores = 5;
    Arachne::setErrorStream(stdout);
    Arachne::init(&argc, argv);

    // First argument specifies a configuration file with the following format
    // <count_of_rows>
    // <time_to_run_in_ns> <attempted_creations_per_second> <thread_duration_in_ns>
    FILE *specFile = fopen(argv[1], "r");
    if (!specFile) {
        printf("Configuration file '%s' non existent!\n", argv[1]);
        exit(1);
    }
    char buffer[1024];
    if (fgets(buffer, 1024, specFile) == NULL) {
        printf("Error reading configuration file: %s\n", strerror(errno));
        exit(1);
    }
    sscanf(buffer, "%zu", &numIntervals);
    intervals = new Interval[numIntervals];
    for (size_t i = 0; i < numIntervals; i++) {
        if (fgets(buffer, 1024, specFile) == NULL) {
            printf("Error reading configuration file: %s\n", strerror(errno));
            exit(1);
        }
        sscanf(buffer, "%lu %lf %lu",
                &intervals[i].timeToRun,
                &intervals[i].creationsPerSecond,
                &intervals[i].durationPerThread);
    }
    fclose(specFile);

    puts("Core Increase Threshold,Core Utilization,Median Latency,99\% Latency,Throughput");
    for (double threshold = 0.0; threshold <= 10.0; threshold+=0.1) {
        // TODO: Vary both the ramp-up and ramp-down factor; otherwise any
        // overly sensitive increase in core count will be rapidly countered by
        // a corresponding decrease.
        // TODO: Discuss with John the policy issues.
        reinterpret_cast<Arachne::DefaultCorePolicy*>(
                Arachne::getCorePolicy())
            ->getEstimator()
            ->setLoadFactorThreshold(threshold);
        Arachne::createThreadWithClass(Arachne::DefaultCorePolicy::EXCLUSIVE, dispatch);
        Arachne::waitForTermination();

        // Sanity check
        if (arrayIndex >= MAX_ENTRIES) {
            puts("Guaranteed memory corruption.");
            abort();
        }

        // Convert latencies to ns
        for (size_t i = 0; i < arrayIndex; i++)
            latencies[i] = Cycles::toNanoseconds(latencies[i]);

        // Output CORE_INCREASE_THRESHOLD,overall core utilization,median,latency,99% latency,throughput
        PerfStats& last = perfStats[indices.size()-1];
        PerfStats& first = perfStats[0];
        double durationOfInterval = Cycles::toSeconds(last.collectionTime - first.collectionTime);
        uint64_t idleCycles = last.idleCycles - first.idleCycles;
        uint64_t totalCycles = last.totalCycles - first.totalCycles;
        double utilization = static_cast<double>(totalCycles - idleCycles) /
            static_cast<double>(totalCycles);
        // Note that this is completed tasks per second, where each task is currently 2 us
        uint64_t throughput = static_cast<uint64_t>(
                static_cast<double>(indices[indices.size() - 1]) / durationOfInterval);

        // Compute load factor.
        uint64_t weightedLoadedCycles = last.weightedLoadedCycles - first.weightedLoadedCycles;
        double loadFactor = static_cast<double>(weightedLoadedCycles) / static_cast<double>(totalCycles);

        // Compute core count changes
        uint64_t numIncrements = last.numCoreIncrements - first.numCoreIncrements;
        uint64_t numDecrements = last.numCoreDecrements - first.numCoreDecrements;

        Statistics mathStats = computeStatistics(latencies, indices[indices.size()-1]);
        printf("%lf,%lf,%lu,%lu,%lu,%lf,%lu,%lu\n", threshold, utilization,
                mathStats.median, mathStats.P99,throughput, loadFactor,
                numIncrements, numDecrements);

        indices.clear();
        perfStats.clear();
        arrayIndex = 0;

        Arachne::init();
    }
    Arachne::shutDown();
    Arachne::waitForTermination();
}
