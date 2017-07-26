#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>

/*
 * This hack will be used to extract segments for an Arachne visualizer used for debugging.
 * It can either extract the events within a time range or the events that are
 * concurrent with a specific event.
 * The input order is currently <creation> <start> <endtime> <coreIds> <TID>
 */

enum EventType {
    CREATION, // Time that a thread is created
    START, // TIme that at thread begins executing user code
    END // Time that a thread returns control to Arachne
};

/**
 * Objects of this type represent a thread being created, beginning to run,
 * and returning control to Arachne. Currently we expect applications to
 * perform their own logging. It is not baked into the thread library to avoid
 * overheads and complexity.
 *
 * Arachne provides only the data structures, and will perform logging of its
 * own internal threads for consistency.
 */
struct Event {
    // Time that this event occurred.
    uint64_t time;
    // A user-specified thread Id.
    uint32_t appThreadId;
    int coreId;
    EventType type;
};

struct Record {
    uint64_t creationTime;
    // This may eventually generalize, for this specific application it is
    // fine.
    uint64_t startTime;
    uint64_t endTime;
    int coreId;
};

int main(int argc, char** argv){

    if (argc < 3) {
        fprintf(stderr, "./ExtractSegment <Events> <TID> [Radius]\n");
        fprintf(stderr, "Only %d arguments given\n", argc);
        for (int i = 0; i < argc; i++) {
            printf("%s\n", argv[i]);
        }
        exit(1);
    }
#define BATCH_SIZE 100
    Event eventBuffer[BATCH_SIZE];

    std::vector<Event> events;

    FILE* input = fopen(argv[1], "r");
    size_t numItems;
    while ((numItems = fread(eventBuffer, sizeof(Event), BATCH_SIZE, input))) {
        for (size_t i = 0; i < numItems; i++) events.push_back(eventBuffer[i]);
        if (numItems < BATCH_SIZE) break;
    }
    fclose(input);

    // Find the start and end time of TID, and output all activity in between
    // by core.
    uint32_t targetTid = atoi(argv[2]);
    uint64_t targetStartTime = 0;
    uint64_t targetEndTime = 0;
    uint64_t base = ULLONG_MAX;

    // Collect all the events in a more friendly format.
    std::unordered_map<uint32_t, Record> recordMap;
    for (size_t i = 0; i < events.size(); i++) {
        uint32_t appThreadId = events[i].appThreadId;
        if (events[i].time < base)
            base = events[i].time;

        if (appThreadId == targetTid) {
            if (events[i].type == CREATION)
                targetStartTime = events[i].time;
            else if (events[i].type == END)
                targetEndTime = events[i].time;
        }
        if (recordMap.find(appThreadId) == recordMap.end()) {
            recordMap[appThreadId] = Record();
        }
        recordMap[appThreadId].coreId = events[i].coreId;
        switch (events[i].type) {
            case CREATION:
                recordMap[appThreadId].creationTime = events[i].time;
                break;
            case START:
                recordMap[appThreadId].startTime = events[i].time;
                break;
            case END:
                recordMap[appThreadId].endTime = events[i].time;
        }
    }

    if (argc == 4) {
        uint64_t cycleRadius = atoll(argv[3]);
        targetStartTime -= cycleRadius;
        targetEndTime += cycleRadius;
    }

    // Normalize this trace.
    std::unordered_set<uint32_t> relevantEvents;
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i].time >= targetStartTime && events[i].time <= targetEndTime)
            relevantEvents.insert(events[i].appThreadId);
    }
    puts("ThreadId,CoreId,CreationTime,StartTime,EndTime,CreationToStart,StartToEnd,CreationToEnd");
    for (const auto& e: relevantEvents) {
        Record& rec = recordMap[e];
        // CoreId,CreationTime,StartTime,EndTime,CreationToStart,StartToEnd,CreationToEnd
        printf("%u,%d,%lu,%lu,%lu,%lu,%lu,%lu\n", e, rec.coreId, rec.creationTime - base,
                rec.startTime - base, rec.endTime - base, rec.startTime -
                rec.creationTime, rec.endTime - rec.startTime, rec.endTime - rec.creationTime);
    }
}

