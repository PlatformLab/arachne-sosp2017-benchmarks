#include <stdlib.h>
#include <atomic>
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "Arachne/Arachne.h"

#define CORE_OCCUPANCY 3

using PerfUtils::Cycles;
using PerfUtils::TimeTrace;

std::atomic<uint64_t> globalCount;

// Both of these times are in cycles
std::atomic<uint64_t> stopTime;
std::atomic<uint64_t> startTime;

// This time is in seconds
std::atomic<double> duration;

Arachne::Semaphore done;

void creator(int id, uint64_t counter) {
    // TODO: timetrace this function
    // TimeTrace::record("Starting creator: %d on %d", id, Arachne::kernelThreadId);
    if (Cycles::rdtsc() < stopTime) {
        // TimeTrace::record("Before createThread: %d on %d", id, Arachne::kernelThreadId);
        if (Arachne::createThread(creator, id, counter + 1) == Arachne::NullThread) {
            abort();
        }
        // TimeTrace::record("After createThread success: %d on %d", id, Arachne::kernelThreadId);
    } else {
        globalCount += counter;
        done.notify();
    }
}

void timeKeeper(int numCores, int seconds) {
    startTime = Cycles::rdtsc();
    uint64_t durationInCycles = Cycles::fromSeconds(seconds);
    stopTime = Cycles::rdtsc() + durationInCycles;
    for (int i = 0; i < numCores * CORE_OCCUPANCY; i++)
        Arachne::createThread(creator, i, 1);
    // Wait for all threads to finish, using a semaphor
    for (int i = 0; i < numCores * CORE_OCCUPANCY; i++)
        done.wait();
    duration = Cycles::toSeconds(Cycles::rdtsc() - startTime);

    std::string outputFilename = "ThreadCreationScalability" + std::to_string(numCores) + ".log";
    // TimeTrace::setOutputFileName(outputFilename.c_str());
    // TimeTrace::print();
    Arachne::PerfStats stats;
    Arachne::PerfStats::collectStats(&stats);
    // number of failed creations normalized by number of cores
//    uint64_t failures = stats.numTimesContended;
//    printf("Number of failed creations on %d cores = %lu, number of successes = %lu, (%lu normalized, fail/success = %f)\n",
//           numCores, failures, globalCount.load(), failures/numCores, (double)failures/(double)globalCount.load());

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
    // printf("%d,%d,%lu\n", numSeconds, numCores, static_cast<uint64_t>(
    //             static_cast<double>(globalCount.load()) / duration));
    return 0;
}
