#include "debug.h"
#include <regex.h>

void regfree(regex_t* preg) {
    warn_unsupported("\nWARNING: regcomp Not Supported\n");
}
