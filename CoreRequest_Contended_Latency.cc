#include <sys/wait.h>
#include <stdio.h>
#include <thread>
#include <atomic>

#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/Logger.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Util.h"
#include "PerfUtils/Stats.h"

using PerfUtils::TimeTrace;
using PerfUtils::Cycles;
using CoreArbiter::CoreArbiterClient;
using namespace CoreArbiter;

#define NUM_TRIALS 1000000

std::atomic<uint64_t> startCycles(0);
uint64_t arrayIndex = 0;
uint64_t latencies[NUM_TRIALS];

void highPriorityRequest(CoreArbiterClient* client,
                         volatile bool* lowPriorityRunning) {
    client->blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client->getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        client->setRequestedCores({1,0,0,0,0,0,0,0});
        while (client->getNumBlockedThreadsFromServer() == 0);
        while (!(*lowPriorityRunning));

        startCycles = Cycles::rdtsc();
        client->setRequestedCores({2,0,0,0,0,0,0,0});
        while(client->getNumBlockedThreads() == 1);
    }

    client->unregisterThread();
}

void highPriorityBlock(CoreArbiterClient* client) {
    client->blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client->getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        while (!client->mustReleaseCore());
        client->blockUntilCoreAvailable();
        uint64_t endCycles = Cycles::rdtsc();
        latencies[arrayIndex++] = endCycles - startCycles;
    }

    client->unregisterThread();
}

void lowPriorityExec(CoreArbiterClient* client,
                     volatile bool* lowPriorityRunning) {
    std::vector<uint32_t> lowPriorityRequest = {0,0,0,0,0,0,0,1};
    client->setRequestedCores(lowPriorityRequest);
    client->blockUntilCoreAvailable();

    // Wait for other process to join
    while (client->getNumProcessesOnServer() == 1);

    for (int i = 0; i < NUM_TRIALS; i++) {
        while (!client->mustReleaseCore());
        *lowPriorityRunning = false;
        client->blockUntilCoreAvailable();
        *lowPriorityRunning = true;
    }

    client->unregisterThread();
}

int main(){
    Logger::setLogLevel(ERROR);

    int sharedMemFd = open("benchmark_sharedmem",
                           O_CREAT | O_RDWR | O_TRUNC, S_IRWXU);
    if (sharedMemFd < 0) {
        fprintf(stderr, "Error opening shared memory page: %s\n",
                strerror(errno));
        return -1;
    }

    if (ftruncate(sharedMemFd, sizeof(bool)) == -1) {
        fprintf(stderr, "Error truncating sharedMemFd: %s\n",
                strerror(errno));
        return -1;
    }
    volatile bool* lowPriorityRunning = (bool*)mmap(NULL, getpagesize(),
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           sharedMemFd, 0);
    if (lowPriorityRunning == MAP_FAILED) {
        fprintf(stderr, "Error on global stats mmap: %s\n", strerror(errno));
        return -1;
    }
    *lowPriorityRunning = false;

    pid_t pid = fork();
    if (pid == 0) {
        CoreArbiterClient* client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");

        // Wait for the low priority thread to be put on a core
        while (client->getNumUnoccupiedCores() == 2);
        client->setRequestedCores({2,0,0,0,0,0,0,0});

        std::thread highPriorityThread1(highPriorityBlock, std::ref(client));
        std::thread highPriorityThread2(highPriorityRequest, std::ref(client),
                                        lowPriorityRunning);
        highPriorityThread1.join();
        highPriorityThread2.join();

        for (int i = 0; i < NUM_TRIALS; i++) {
            latencies[i] = Cycles::toNanoseconds(latencies[i]);
        }
        printStatistics("core_request_cooperative_latencies", latencies, NUM_TRIALS, "data");
    } else  {
        CoreArbiterClient* client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");
        lowPriorityExec(client, lowPriorityRunning);

        wait(NULL);
    }
}
