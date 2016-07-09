#include "debug.h"
#include <regex.h>

size_t regerror(int errcode, const regex_t* preg, char* errbuf,
                size_t errbuf_size) {
    panic("\nFATAL: regerror Not Supported\n");
    return 0;
}
