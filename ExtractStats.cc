#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include "Stats.h"

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != NULL)
            result += buffer.data();
    }
    return result;
}

int main(int argc, const char** argv){
    // Second argument is line count.
    if (argc < 2) {
        printf("Usage: ExtractStats.cc <FileName>\n");
        exit(1);
    }
    char cmd[1024];
    sprintf(cmd, "wc -l  \"%s\" | awk '{print $1}'", argv[1]);
    std::string output = exec(cmd);
    size_t lineCount;
    sscanf(output.c_str(), "%zu\n", &lineCount);
    uint64_t *buffer = new uint64_t[lineCount];

    FILE* f = fopen(argv[1], "r");
    for (size_t i = 0; i < lineCount; i++) {
        fscanf(f, "%lu\n", buffer + i);
    }
    fclose(f);
    printStatistics(argv[1], buffer, lineCount, NULL);
}

