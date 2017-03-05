#include <stdio.h>
#include <thread>
#include <atomic>

#include "CoreArbiterClient.h"
#include "Cycles.h"
#include "TimeTrace.h"
#include "Util.h"

using PerfUtils::TimeTrace;
using CoreArbiter::CoreArbiterClient;

#define NUM_TRIALS 100000

std::atomic<bool> aboutToBlock(true);
/**
  * This thread will get unblocked when a core is allocated, and will block
  * itself again when the number of cores is decreased.
  */
void coreExec(CoreArbiterClient& client) {
    for (int i = 0; i < NUM_TRIALS; i++) {
        aboutToBlock = true;
        TimeTrace::record("Core thread about to block");
        client.blockUntilCoreAvailable();
        TimeTrace::record("Core thread returned from block");
        aboutToBlock = false;
        while (!client.shouldReleaseCore());
        TimeTrace::record("Core informed that it should block");
    }
}

int main(){
    CoreArbiterClient& client =
        CoreArbiterClient::getInstance("/tmp/CoreArbiter/testsocket");
    std::thread coreThread(coreExec, std::ref(client));

    std::vector<core_t> oneCoreRequest = {1,0,0,0,0,0,0,0};
    std::vector<core_t> zeroCoreRequest = {0,0,0,0,0,0,0,0};
    for (int i = 0; i < NUM_TRIALS; i++) {
        // Release a core and wait for release
        while (!aboutToBlock);
        TimeTrace::record("Requesting a core");
        client.setNumCores(oneCoreRequest);
        while (aboutToBlock);
        TimeTrace::record("Releasing a core");
        client.setNumCores(zeroCoreRequest);
    }
    coreThread.join();
    TimeTrace::setOutputFileName("CoreRequest_Noncontended.log");
    TimeTrace::print();
}
