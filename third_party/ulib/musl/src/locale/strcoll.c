#include "libc.h"
#include <string.h>

int strcoll(const char* l, const char* r) {
    return strcmp(l, r);
}
