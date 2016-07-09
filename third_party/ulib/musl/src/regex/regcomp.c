#include "debug.h"
#include <regex.h>

int regcomp(regex_t* preg, const char* pattern, int cflags) {
    panic("\nFATAL: regcomp Not Supported\n");
    return 0;
}
