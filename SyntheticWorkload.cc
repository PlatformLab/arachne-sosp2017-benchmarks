#include <random>
#include <thread>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "Arachne.h"
#include "ThreadActivityTracker.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Stats.h"
#include "PerfUtils/Util.h"
#include "CoreArbiter/Logger.h"
#include "CoreArbiter/CoreArbiterClient.h"

using PerfUtils::Cycles;
using Arachne::PerfStats;
using CoreArbiter::CoreArbiterClient;

namespace Arachne{
extern bool disableLoadEstimation;
extern double maxIdleCoreFraction;
extern double loadFactorThreshold;
extern double maxUtilization;
extern std::vector<std::atomic<MaskAndCount> * > occupiedAndCount;
extern CoreArbiterClient& coreArbiter;
}

using PerfUtils::TimeTrace;

int ARRAY_EXP = 26;
size_t MAX_ENTRIES;

//uint64_t *creationTimes;
//uint64_t *startTimes;
//uint64_t *endTimes;
uint64_t *latencies;

// Fast reconstruction of data
// ThreadID is implied by array index.
int *coreIds;

enum DistributionType {
    POISSON,
    UNIFORM
} distType = POISSON;

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
std::vector<uint64_t> numTimesLoadClipped;

// The performance statistics before each change in load, which we can use
// later to compute utilization.
std::vector<PerfStats> perfStats;

/**
  * Spin for duration cycles, and then compute latency from creation time.
  */
void fixedWork(uint64_t duration, uint64_t creationTime, uint32_t arrayIndex) {
    uint64_t startTime = Cycles::rdtsc();
    uint64_t stop = startTime + duration;
    while (Cycles::rdtsc() < stop);
    uint64_t endTime = Cycles::rdtsc();
    uint64_t latency = endTime - creationTime;

//    creationTimes[arrayIndex] = creationTime;
//    startTimes[arrayIndex] = startTime;
//    endTimes[arrayIndex] = endTime;
    latencies[arrayIndex] = latency;
//    coreIds[arrayIndex] = Arachne::coreArbiter.getCoreId();
    uint32_t coreId = Arachne::coreArbiter.getCoreId();
    Arachne::logEvent(creationTime, arrayIndex, coreId, Arachne::CREATION);
    Arachne::logEvent(startTime, arrayIndex, coreId, Arachne::START);
    Arachne::logEvent(endTime, arrayIndex, coreId, Arachne::END);
}

void postProcessResults(const char* benchmarkFile, uint64_t arrayIndex) {
    // Output TimeTrace for human reading
    size_t index = rindex(benchmarkFile, static_cast<int>('.')) - benchmarkFile;
    char outTraceFileName[1024];
    strncpy(outTraceFileName, benchmarkFile, index);
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

//    fprintf(stderr, "Before writing latencies\n");
    // Convert latencies to ns, and also record original values.
    FILE* Output = fopen("/tmp/Latency.data", "w");
    fwrite(latencies, sizeof(uint64_t), arrayIndex, Output);
    fclose(Output);

//    fprintf(stderr, "Outputed Latency array\n");

//    Output = fopen("/tmp/CreationTime.data", "w");
//    fwrite(creationTimes, sizeof(uint64_t), arrayIndex, Output);
//    fclose(Output);
//
////    fprintf(stderr, "Outputed creation array\n");
//
//    Output = fopen("/tmp/StartTime.data", "w");
//    fwrite(startTimes, sizeof(uint64_t), arrayIndex, Output);
//    fclose(Output);
//
////    fprintf(stderr, "Outputed start array\n");
//
//    Output = fopen("/tmp/EndTime.data", "w");
//    fwrite(endTimes, sizeof(uint64_t), arrayIndex, Output);
//    fclose(Output);
////    fprintf(stderr, "Outputed end array\n");
//
//    Output = fopen("/tmp/CoreId.data", "w");
//    fwrite(coreIds, sizeof(int), arrayIndex, Output);
//    fclose(Output);
//

//    fprintf(stderr, "Outputed CoreId array\n");
//    for (size_t i = 0; i < arrayIndex; i++) {
//        latencies[i] = Cycles::toNanoseconds(latencies[i]);
//    }

    FILE* events = fopen("/tmp/Events.data", "w");
    Arachne::dumpEvents(events);
    fclose(events);
    // Output core utilization, median & 99% latency, and throughput for each interval in a
    // plottable format.
    puts("Duration,Offered Load,Core Utilization,Absolute Cores Used,50\% Latency,90\%,99\%,Max,Throughput,Load Factor,Core++,Core--,Load Clips,U x LF,(1-idle) x LF");
    for (size_t i = 1; i < indices.size(); i++) {
        double durationOfInterval = Cycles::toSeconds(perfStats[i].collectionTime -
            perfStats[i-1].collectionTime);
        uint64_t idleCycles = perfStats[i].idleCycles - perfStats[i-1].idleCycles;
        uint64_t totalCycles = perfStats[i].totalCycles - perfStats[i-1].totalCycles;
        double utilization = static_cast<double>(totalCycles - idleCycles) /
            static_cast<double>(totalCycles);
        double coresUsed = static_cast<double>(totalCycles - idleCycles) /
            static_cast<double>(perfStats[i].collectionTime -
                    perfStats[i-1].collectionTime);


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

        // Compute clipping.
        uint64_t loadClipCount = numTimesLoadClipped[i] - numTimesLoadClipped[i-1];

        // Median and 99% Latency
        // Note that this computation will modify data
        Statistics mathStats = computeStatistics(latencies + indices[i-1], indices[i] - indices[i-1]);

        // Convert statistics output to nanoseconds
//        mathStats.median  =       Cycles::toNanoseconds(mathStats.median);
//        mathStats.P90     =       Cycles::toNanoseconds(mathStats.P90);
//        mathStats.P99     =       Cycles::toNanoseconds(mathStats.P99);
//        mathStats.max     =       Cycles::toNanoseconds(mathStats.max);

        printf("%lf,%lf,%lf,%lf,%lu,%lu,%lu,%lu,%lu,%lf,%lu,%lu,%lu,%lf,%lf\n", durationOfInterval,
                intervals[i-1].creationsPerSecond, utilization, coresUsed,
                mathStats.median, mathStats.P90, mathStats.P99, mathStats.max,
                throughput, loadFactor, numIncrements, numDecrements, loadClipCount,
                utilization * loadFactor, (1-totalIdleCores)*loadFactor);
    }
}
void dispatch(const char* benchmarkFile) {
    // Prevent scheduling onto this core, since threads scheduled to this core
    // will never get a chance to run.
	Arachne::makeExclusiveOnCore();
    uint32_t arrayIndex = 0;

    // Page in our data store
    MAX_ENTRIES = 1L << ARRAY_EXP;
    latencies = new uint64_t[MAX_ENTRIES];
    memset(latencies, 0, MAX_ENTRIES*sizeof(uint64_t));

////    fprintf(stderr, "Initialized latencies array\n");
//
//    creationTimes = new uint64_t[MAX_ENTRIES];
//    memset(creationTimes, 0, MAX_ENTRIES*sizeof(uint64_t));
//
////    fprintf(stderr, "Initialized creations array\n");
//
//    startTimes = new uint64_t[MAX_ENTRIES];
//    memset(startTimes, 0, MAX_ENTRIES*sizeof(uint64_t));
//
////    fprintf(stderr, "Initialized startTime array\n");
//
//    endTimes = new uint64_t[MAX_ENTRIES];
//    memset(endTimes, 0, MAX_ENTRIES*sizeof(uint64_t));
//
////    fprintf(stderr, "Initialized endTime array\n");
//
//    coreIds = new int[MAX_ENTRIES];
//    memset(coreIds, 0, MAX_ENTRIES*sizeof(int));

//    fprintf(stderr, "Initialized CoreIds\n");
    PerfUtils::Util::serialize();

    // Initialize interval
    size_t currentInterval = 0;

    // Start with a DCFT-style implementation based on shared-memory for communication
    // If that becomes too expensive, then switch to a separate thread for commands.
    uint64_t cyclesPerThread = Cycles::fromNanoseconds(intervals[currentInterval].durationPerThread);

	std::random_device rd;
	std::mt19937 gen(rd());

    std::exponential_distribution<double> intervalGenerator(intervals[currentInterval].creationsPerSecond);
    std::uniform_real_distribution<> uniformIG(0, 2.0 / intervals[currentInterval].creationsPerSecond);

    uint64_t nextCycleTime;
    switch (distType) {
        case POISSON:
            nextCycleTime = Cycles::rdtsc() +
                Cycles::fromSeconds(intervalGenerator(gen));
            break;
        case UNIFORM:
            nextCycleTime = Cycles::rdtsc() +
                Cycles::fromSeconds(uniformIG(gen));
    }


    PerfStats stats;
    PerfStats::collectStats(&stats);
    uint64_t loadClipCount = 0;

    indices.push_back(arrayIndex);
    numTimesLoadClipped.push_back(loadClipCount);
    perfStats.push_back(stats);

    uint64_t currentTime = Cycles::rdtsc();
    uint64_t nextIntervalTime = currentTime +
        Cycles::fromNanoseconds(intervals[currentInterval].timeToRun);

    TimeTrace::record("Beginning of benchmark");

    // DCFT loop
    for (;; currentTime = Cycles::rdtsc()) {
        if (nextCycleTime < currentTime) {
            uint64_t targetIndex = arrayIndex++;
            if (targetIndex > MAX_ENTRIES) {
                fprintf(stderr, "Death by out of bounds\n");
                exit(0);
            }
            // Keep trying to create this thread until we succeed.
            while (Arachne::createThread(fixedWork, cyclesPerThread, nextCycleTime, targetIndex) == Arachne::NullThread);

            switch(distType) {
                case POISSON:
                    nextCycleTime = nextCycleTime +
                        Cycles::fromSeconds(intervalGenerator(gen));
                    break;
               case UNIFORM:
                    nextCycleTime = nextCycleTime +
                        Cycles::fromSeconds(uniformIG(gen));

            }
            // Clip the nextCycle time to the current time if we're about to go
            // into instability. This way, we do not keep falling further and
            // further behind and have latency proportional to the running time
            // of the experiment but we do create threads as fast as possible
            // when we are not meeting the threshold.
            if (nextCycleTime < currentTime) {
                nextCycleTime = currentTime;
                loadClipCount++;
            }
        }

        if (nextIntervalTime < currentTime) {
            // Collect latency, throughput, and core utilization information from the past interval
            PerfStats::collectStats(&stats);
            indices.push_back(arrayIndex);
            numTimesLoadClipped.push_back(loadClipCount);
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
            switch(distType) {
              case POISSON:
                intervalGenerator.param(
                        std::exponential_distribution<double>::param_type(
                            intervals[currentInterval].creationsPerSecond));
                nextCycleTime = Cycles::rdtsc() +
                    Cycles::fromSeconds(intervalGenerator(gen));
                break;
              case UNIFORM:
                uniformIG.param(
                        std::uniform_real_distribution<double>::param_type(
                            0, 2.0 / intervals[currentInterval].creationsPerSecond));
                nextCycleTime = Cycles::rdtsc() +
                    Cycles::fromSeconds(uniformIG(gen));
            }
            TimeTrace::record("Load Change END %u --> %u Creations Per Second.",
                    static_cast<uint32_t>(intervals[currentInterval - 1].creationsPerSecond),
                    static_cast<uint32_t>(intervals[currentInterval].creationsPerSecond));

        }
    }
    fprintf(stderr, "Finished producing dispatches %lu\n", Cycles::rdtsc());
    // Wait for completions so the checksum will be useful. Exactly two threads
    // should exist when this loop exits. One is the core scaling thread and
    // one is this thread.
    while (true) {
        uint64_t sum = 0;
        for (size_t i = 0; i < Arachne::occupiedAndCount.size(); i++)
            sum += __builtin_popcountl(Arachne::occupiedAndCount[i]->load().occupied);
        if (sum == 2) break;
    }

    // We can compute statistics and then shut down since we already stored the
    // last index of the last interval.
    postProcessResults(benchmarkFile, arrayIndex);

//    // Verify checksum after Stats
//    newCheckSum = 0;
//    for (size_t i = 0; i < arrayIndex; i++)
//        newCheckSum += latencies[i];
//    if (newCheckSum != beforeStatsChecksum) {
//        fprintf(stderr, "Checksum failed!Previous = %lu, After Stats = %lu\n", beforeStatsChecksum, newCheckSum);
//        fflush(stderr);
//        exit(0);
//    }
    delete[] latencies;

    Arachne::makeSharedOnCore();
    Arachne::shutDown();
}

/**
  * This method attempts to attach gdb to the currently running process.
  */
void invokeGDB(int signum) {
    // Ensure only one invocation of gdb tries to attach to this process
    static std::atomic<int> invokedGdb(0);
    if (invokedGdb) return;
    invokedGdb = 1;

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
  * This function currently supports only long options.
  */
void
parseOptions(int* argcp, const char** argv) {
    if (argcp == NULL) return;

    int argc = *argcp;

    struct OptionSpecifier {
        // The string that the user uses after `--`.
        const char* optionName;
        // The id for the option that is returned when it is recognized.
        int id;
        // Does the option take an argument?
        bool takesArgument;
    } optionSpecifiers[] = {
        {"arraySize", 'a', true},
        {"distribution", 'd', true},
        {"scalingThreshold", 's', true}
    };
    const int UNRECOGNIZED = ~0;

    int i = 1;
    while (i < argc) {
        if (argv[i][0] != '-' || argv[i][1] != '-') {
            i++;
            continue;
        }
        const char* optionName = argv[i] + 2;
        int optionId = UNRECOGNIZED;
        const char* optionArgument = NULL;

        for (size_t k = 0;
                k < sizeof(optionSpecifiers) / sizeof(OptionSpecifier); k++) {
            const char* candidateName = optionSpecifiers[k].optionName;
            bool needsArg = optionSpecifiers[k].takesArgument;
            if (strncmp(candidateName,
                        optionName, strlen(candidateName)) == 0) {
                if (needsArg) {
                    if (i + 1 >= argc) {
                        fprintf(stderr,
                                "Missing argument to option %s!\n",
                                candidateName);
                        break;
                    }
                    optionArgument = argv[i+1];
                    optionId = optionSpecifiers[k].id;
                    argc -= 2;
                    memmove(argv + i, argv + i + 2, (argc - i) * sizeof(char*));
                } else {
                    optionId = optionSpecifiers[k].id;
                    argc -= 1;
                    memmove(argv + i, argv + i + 1, (argc - i) * sizeof(char*));
                }
                break;
            }
        }
        switch (optionId) {
            case 'a':
                ARRAY_EXP = atoi(optionArgument);
                break;
            case 'd':
                if (optionArgument[0] == 'p')
                    distType = POISSON;
                else if (optionArgument[0] == 'u')
                    distType = UNIFORM;
                else {
                    fprintf(stderr, "Unrecognized distribution option %s!\n",
                            optionArgument);
                    abort();
                }
                break;
            case 's':
                Arachne::maxIdleCoreFraction = atof(optionArgument);
                Arachne::loadFactorThreshold =  atof(optionArgument);
                Arachne::maxUtilization = atof(optionArgument);
                break;
            case UNRECOGNIZED:
                fprintf(stderr, "Unrecognized option %s given.", optionName);
                abort();
        }
    }
    *argcp = argc;
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
    CoreArbiter::Logger::setLogLevel(CoreArbiter::NOTICE);
    Arachne::Logger::setLogLevel(Arachne::DEBUG);

	Arachne::minNumCores = 2;
	Arachne::maxNumCores = 5;
    Arachne::setErrorStream(stderr);
    Arachne::init(&argc, argv);

    // Parse options such as the size of array in powers of 2, and what kind of
    // distribution to use, and the value of the threshold parameter to pass to
    // Arachne.
    parseOptions(&argc, argv);

    if (argc < 2) {
        printf("Please specify a configuration file!\n");
        exit(1);
    }

    // First argument specifies a configuration file with the following format
    // <count_of_rows>
    // <time_to_run_in_ns> <attempted_creations_per_second> <thread_duration_in_ns>
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
        sscanf(buffer, "%lu %lf %lu",
                &intervals[i].timeToRun,
                &intervals[i].creationsPerSecond,
                &intervals[i].durationPerThread);
//        // Adjust the offered load down to match the maximum Poisson load achieved.
//        if (distType == UNIFORM)
//            // For 70% utilization threshold and load factor 225
//            // intervals[i].creationsPerSecond *= 0.87;
//            // For 85% utilization threshold, the maximum throughput ratio for
//            // poisson is this much.
//            intervals[i].creationsPerSecond *= 0.9327956;
    }
    fclose(specFile);

    // Catch intermittent errors
    installSignalHandler();
    Arachne::createThread(dispatch, argv[1]);
    Arachne::waitForTermination();
}
