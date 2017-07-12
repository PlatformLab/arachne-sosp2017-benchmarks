#include <stdio.h>
#include <thread>
#include <atomic>

#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/Logger.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Util.h"

using PerfUtils::TimeTrace;
using CoreArbiter::CoreArbiterClient;
using namespace CoreArbiter;

#define NUM_TRIALS 1000

/**
  * This thread will get unblocked when a core is allocated, and will block
  * itself again when the number of cores is decreased.
  */
void coreExec(CoreArbiterClient& client) {
    for (int i = 0; i < NUM_TRIALS; i++) {
        TimeTrace::record("Core thread about to block");
        client.blockUntilCoreAvailable();
        TimeTrace::record("Core thread returned from block");
        while (!client.mustReleaseCore());
        TimeTrace::record("Core informed that it should block");
    }
}

void coreRequest(CoreArbiterClient& client) {
    std::vector<uint32_t> oneCoreRequest = {1,0,0,0,0,0,0,0};

    client.setRequestedCores(oneCoreRequest);
    client.blockUntilCoreAvailable();

    std::vector<uint32_t> twoCoresRequest = {2,0,0,0,0,0,0,0};
    for (int i = 0; i < NUM_TRIALS; i++) {
        // When the number of blocked threads becomes nonzero, we request a core.
        while (client.getNumBlockedThreads() == 0);
        TimeTrace::record("Detected thread block");
        client.setRequestedCores(twoCoresRequest);
        TimeTrace::record("Requested a core");
        // When the number of blocked threads becomes zero, we release a core.
        while (client.getNumBlockedThreads() == 1);
        TimeTrace::record("Detected thread wakeup");
        client.setRequestedCores(oneCoreRequest);
        TimeTrace::record("Released a core");
    }
}

int main(){
    Logger::setLogLevel(ERROR);
    CoreArbiterClient& client =
        CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");
    std::thread requestThread(coreRequest, std::ref(client));
    while (client.getNumOwnedCores() == 0);

    std::thread coreThread(coreExec, std::ref(client));

    coreThread.join();
    requestThread.join();

    TimeTrace::setOutputFileName("CoreRequest_Noncontended.log");
    TimeTrace::print();
}
