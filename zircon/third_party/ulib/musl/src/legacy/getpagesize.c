#define _GNU_SOURCE
#include "libc.h"
#include <unistd.h>

int getpagesize(void) {
    return PAGE_SIZE;
}
