#include "debug.h"
#include <regex.h>

int regexec(const regex_t* preg, const char* string, size_t nmatch,
            regmatch_t pmatch[], int eflags) {
    warn_unsupported("\nWARNING: regcomp Not Supported\n");
    return REG_NOMATCH;
}
