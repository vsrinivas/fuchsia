#include "debug.h"
#include <regex.h>

int regexec(const regex_t* preg, const char* string, size_t nmatch,
            regmatch_t pmatch[], int eflags) {
    panic("\nFATAL: regexec Not Supported\n");
    return 0;
}
