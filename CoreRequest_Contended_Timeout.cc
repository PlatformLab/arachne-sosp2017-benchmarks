#include <stdio.h>
#include <thread>
#include <atomic>

#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/Logger.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Util.h"

using PerfUtils::TimeTrace;
using PerfUtils::Cycles;
using CoreArbiter::CoreArbiterClient;
using namespace CoreArbiter;

#define NUM_TRIALS 1000

void highPriorityRequest(CoreArbiterClient& client) {
    client.blockUntilCoreAvailable();

    // Wait until the other high priority thread is running
    while (client.getNumOwnedCores() < 2);

    for (int i = 0; i < NUM_TRIALS; i++) {
        // printf("*** %d ***\n", i);
        TimeTrace::record("About to request fewer cores");
        client.setRequestedCores({1,0,0,0,0,0,0,0});
        TimeTrace::record("Requested fewer cores");
        while (client.getNumBlockedThreadsFromServer() == 0);
        TimeTrace::record("High priority thread blocked.");
        while(client.getNumUnoccupiedCores() > 0);
        TimeTrace::record("About to request more cores");
        client.setRequestedCores({2,0,0,0,0,0,0,0});
        TimeTrace::record("Requested more cores");
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
        TimeTrace::record("High priority core release requested.");
        client.blockUntilCoreAvailable();
        TimeTrace::record("High priority core acquired.");
    }

    client.unregisterThread();
}

void lowPriorityExec(CoreArbiterClient& client) {
    std::vector<uint32_t> lowPriorityRequest = {0,0,0,0,0,0,0,1};
    client.setRequestedCores(lowPriorityRequest);
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
        client.setRequestedCores({2,0,0,0,0,0,0,0});

        std::thread highPriorityThread1(highPriorityBlock, std::ref(client));
        std::thread highPriorityThread2(highPriorityRequest, std::ref(client));
        highPriorityThread1.join();
        highPriorityThread2.join();

        TimeTrace::setOutputFileName("CoreRequest_Contended_Timeout.log");
        TimeTrace::print();
    } else  {
        CoreArbiterClient& client =
            CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");
        lowPriorityExec(client);

        wait();

        fflush(stdout);
    }
}
