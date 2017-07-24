#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


int ARRAY_EXP = 26;
size_t MAX_ENTRIES;
/*
 * This hack will be used to extract segments for an Arachne visualizer used for debugging.
 * It can either extract the events within a time range or the events that are
 * concurrent with a specific event.
 * The input order is currently <creation> <start> <endtime> <coreIds> <TID>
 */

int main(int argc, char** argv){

    if (argc < 6) {
        fprintf(stderr, "./ExtractSegment <creation> <start> <endtime> <coreIds> <TID>\n");
        fprintf(stderr, "Only %d arguments given\n", argc);
        for (int i = 0; i < argc; i++) {
            printf("%s\n", argv[i]);
        }
        exit(1);
    }
    MAX_ENTRIES = 1L << ARRAY_EXP;
    uint64_t *creationTimes = new uint64_t[MAX_ENTRIES];
    uint64_t *startTimes = new uint64_t[MAX_ENTRIES];
    uint64_t *endTimes = new uint64_t[MAX_ENTRIES];
    int *coreIds = new int[MAX_ENTRIES];

    size_t numItems = 0;

    FILE* input = fopen(argv[1], "r");
    numItems = fread(creationTimes, sizeof(uint64_t), MAX_ENTRIES, input);
    fclose(input);

    input = fopen(argv[2], "r");
    fread(startTimes, sizeof(uint64_t), MAX_ENTRIES, input);
    fclose(input);

    input = fopen(argv[3], "r");
    fread(endTimes, sizeof(uint64_t), MAX_ENTRIES, input);
    fclose(input);

    input = fopen(argv[4], "r");
    fread(coreIds, sizeof(int), MAX_ENTRIES, input);
    fclose(input);

    // Find the start and end time of TID, and output all activity in between
    // by core.
    int targetTid = atoi(argv[5]);
    uint64_t targetStartTime = creationTimes[targetTid];
    uint64_t targetEndTime = endTimes[targetTid];

    if (argc == 7) {
        uint64_t cycleRadius = atoll(argv[6]);
        targetStartTime -= cycleRadius;
        targetEndTime += cycleRadius;
    }

    // Normalize this trace.
    uint64_t base = creationTimes[0];
    puts("ThreadId,CoreId,CreationTime,StartTime,EndTime,CreationToStart,StartToEnd,CreationToEnd");
    for (size_t i = 0; i < numItems; i++) {
        if (endTimes[i] < targetStartTime) continue;
        if (creationTimes[i] > targetEndTime) continue;
        // CoreId,CreationTime,StartTime,EndTime,CreationToStart,StartToEnd,CreationToEnd
        printf("%lu,%d,%lu,%lu,%lu,%lu,%lu,%lu\n", i, coreIds[i], creationTimes[i] - base,
                startTimes[i] - base, endTimes[i] - base, startTimes[i] -
                creationTimes[i], endTimes[i] - startTimes[i], endTimes[i] - creationTimes[i]);
    }
}

