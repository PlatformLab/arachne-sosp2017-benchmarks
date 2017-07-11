#include <stdio.h>
#include <thread>
#include <atomic>

#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/Logger.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Util.h"
#include "Stats.h"

using PerfUtils::TimeTrace;
using PerfUtils::Cycles;
using CoreArbiter::CoreArbiterClient;
using namespace CoreArbiter;

#define NUM_TRIALS 100000

std::atomic<uint64_t> startCycles(0);
uint64_t arrayIndex = 0;
uint64_t latencies[NUM_TRIALS];

void highPriorityRequest(CoreArbiterClient& client) {
    client.blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client.getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        printf("%d\n", i);
        client.setNumCores({1,0,0,0,0,0,0,0});
        while (client.getNumBlockedThreadsFromServer() == 0);
        while(client.getNumUnoccupiedCores() > 0);

        startCycles = Cycles::rdtsc();
        client.setNumCores({2,0,0,0,0,0,0,0});
        while(client.getNumBlockedThreads() == 1);
    }

    client.unregisterThread();
}

void highPriorityBlock(CoreArbiterClient& client) {
    client.blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client.getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        while (!client.mustReleaseCore());
        client.blockUntilCoreAvailable();
        uint64_t endCycles = Cycles::rdtsc();
        latencies[arrayIndex++] = endCycles - startCycles;
    }

    client.unregisterThread();
}

void lowPriorityExec(CoreArbiterClient& client) {
    std::vector<uint32_t> lowPriorityRequest = {0,0,0,0,0,0,0,1};
    client.setNumCores(lowPriorityRequest);
    client.blockUntilCoreAvailable();

    while (client.getNumProcessesOnServer() == 1);
    while (client.getNumProcessesOnServer() == 2);

    client.unregisterThread();
}

int main(){
    Logger::setLogLevel(ERROR);

    pid_t pid = fork();
    if (pid == 0) {
        CoreArbiterClient& client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");

        // Wait for the low priority thread to be put on a core
        while (client.getNumUnoccupiedCores() == 2);
        client.setNumCores({2,0,0,0,0,0,0,0});

        std::thread highPriorityThread1(highPriorityBlock, std::ref(client));
        std::thread highPriorityThread2(highPriorityRequest, std::ref(client));
        highPriorityThread1.join();
        highPriorityThread2.join();

        for (int i = 0; i < NUM_TRIALS; i++) {
            latencies[i] = Cycles::toNanoseconds(latencies[i]);
        }
        printStatistics("core_request_timeout_latencies", latencies, NUM_TRIALS, "data");

    } else  {
        CoreArbiterClient& client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");
        lowPriorityExec(client);

        wait();

        fflush(stdout);
    }
}
