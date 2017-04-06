#include <stdio.h>
#include <thread>
#include <atomic>
#include <sys/wait.h>

#include "CoreArbiterClient.h"
#include "Logger.h"
#include "Cycles.h"
#include "TimeTrace.h"
#include "Util.h"

using PerfUtils::TimeTrace;
using PerfUtils::Cycles;
using CoreArbiter::CoreArbiterClient;
using namespace CoreArbiter;

#define NUM_TRIALS 1000

void highPriorityRequest(CoreArbiterClient& client,
                         std::atomic<bool>* lowPriorityRunning) {
    client.blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client.getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        client.setNumCores({1,0,0,0,0,0,0,0});
        LOG(ERROR, "Requested fewer cores. ID = %d\n", i);
        while (client.getNumBlockedThreadsFromServer() == 0);
        while (!(*lowPriorityRunning));
        client.setNumCores({2,0,0,0,0,0,0,0});
        LOG(ERROR, "Requested more cores. ID = %d\n", i);
        uint32_t numBlockedThreads;
        while(*(lowPriorityRunning));
        do {
            numBlockedThreads = client.getNumBlockedThreads();
        } while(numBlockedThreads == 1);
        // while(client.getNumBlockedThreads() == 1);
    }

    LOG(ERROR, "******************\n");
    client.unregisterThread();
}

void highPriorityBlock(CoreArbiterClient& client) {
    client.blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client.getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        while (!client.mustReleaseCore());
        LOG(ERROR, "High priority blocking. ID = %d\n", i);
        client.blockUntilCoreAvailable();
        LOG(ERROR, "High priority unblocked. ID = %d\n", i);
    }

    LOG(ERROR, "!!!!!!!!!!!!!!!\n");
    client.unregisterThread();
}

void lowPriorityExec(CoreArbiterClient& client,
                     std::atomic<bool>* lowPriorityRunning) {
    std::vector<uint32_t> lowPriorityRequest = {0,0,0,0,0,0,0,1};
    client.setNumCores(lowPriorityRequest);
    client.blockUntilCoreAvailable();

    *lowPriorityRunning = true;

    // Wait for other process to join
    // while (client.getNumProcessesOnServer() == 1);

    for (int i = 0; i < NUM_TRIALS; i++) {
        LOG(ERROR, "### %d ###\n", i);
        while (!client.mustReleaseCore());
        LOG(ERROR, "Low priority blocking. ID = %d\n", i);
        *lowPriorityRunning = false;
        client.blockUntilCoreAvailable();
        LOG(ERROR, "Low priority unblocked. ID = %d\n", i);
        *lowPriorityRunning = true;
    }

    while (!client.mustReleaseCore());
    LOG(ERROR, "Low priority thread exiting\n");
    *lowPriorityRunning = false;

    LOG(ERROR, "################\n");
    client.unregisterThread();
}

int main(){
    Logger::setLogLevel(WARNING);

    int sharedMemFd = open("benchmark_sharedmem",
                           O_CREAT | O_RDWR | O_TRUNC, S_IRWXU);
    if (sharedMemFd < 0) {
        LOG(ERROR, "Error opening shared memory page: %s\n",
                strerror(errno));
        return -1;
    }

    ftruncate(sharedMemFd, sizeof(std::atomic<bool>));
    std::atomic<bool>* lowPriorityRunning = (std::atomic<bool>*)mmap(NULL, getpagesize(),
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           sharedMemFd, 0);
    if (lowPriorityRunning == MAP_FAILED) {
        LOG(ERROR, "Error on global stats mmap: %s\n", strerror(errno));
        return -1;
    }
    *lowPriorityRunning = false;

    pid_t pid = fork();
    if (pid == 0) {
        CoreArbiterClient& client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");

        // Wait for the low priority thread to be put on a core
        while (client.getNumUnoccupiedCores() == 2);
        client.setNumCores({2,0,0,0,0,0,0,0});

        std::thread highPriorityThread1(highPriorityBlock, std::ref(client));
        std::thread highPriorityThread2(highPriorityRequest, std::ref(client),
                                        lowPriorityRunning);
        highPriorityThread1.join();
        highPriorityThread2.join();
        client.unregisterThread();

        LOG(ERROR, "@@@@@@@ AFTER JOIN @@@@@@@@@@@\n");

        // TimeTrace::setOutputFileName("CoreRequest_Contended_HighPriority.log");
        // TimeTrace::print();
    } else  {
        CoreArbiterClient& client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");
        lowPriorityExec(client, lowPriorityRunning);

        // TimeTrace::setOutputFileName("CoreRequest_Contended_LowPriority.log");
        // TimeTrace::print();

        if (wait(NULL) < 0) {
            LOG(ERROR, "Error on wait: %s\n", strerror(errno));
        }

    }
}
