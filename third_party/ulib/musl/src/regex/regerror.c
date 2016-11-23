#include <regex.h>

#include <stddef.h>
#include <string.h>

#include "debug.h"

size_t regerror(int errcode, const regex_t* preg, char* errbuf,
                size_t errbuf_size) {
    const char* err_string = "regex is not yet supported";

    if (errbuf != NULL) {
        strncpy(errbuf, err_string, errbuf_size);
        if (errbuf_size > 0) {
            errbuf[errbuf_size - 1] = 0;
        }
    }

    warn_unsupported("\nWARNING: regcomp Not Supported\n");

    // regerror returns the length of the buffer needed to hold the
    // NUL terminmated message.
    return strlen(err_string) + 1;
}
