#include "debug.h"
#include <regex.h>

int regcomp(regex_t* preg, const char* pattern, int cflags) {
    warn_unsupported("\nWARNING: regcomp Not Supported\n");
    return REG_ENOSYS;
}
