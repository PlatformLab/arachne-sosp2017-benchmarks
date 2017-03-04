#include <stdio.h>
#include <thread>
#include <atomic>

#include "CoreArbiterClient.h"
#include "Cycles.h"
#include "TimeTrace.h"
#include "Util.h"

using PerfUtils::TimeTrace;
using CoreArbiter::CoreArbiterClient;

#define NUM_TRIALS 1

std::atomic<bool> aboutToBlock(true);
/**
  * This thread will get unblocked when a core is allocated, and will block
  * itself again when the number of cores is decreased.
  */
void coreExec(CoreArbiterClient& client) {
    for (int i = 0; i < NUM_TRIALS; i++) {
        aboutToBlock = true;
        client.blockUntilCoreAvailable();
        aboutToBlock = false;
        while (!client.shouldReleaseCore());
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
        client.setNumCores(oneCoreRequest);
        while (aboutToBlock);
        client.setNumCores(zeroCoreRequest);
    }
    coreThread.join();
}
