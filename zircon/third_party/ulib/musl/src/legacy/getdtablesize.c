#define _GNU_SOURCE
#include <limits.h>
#include <unistd.h>

int getdtablesize(void) {
    int max = sysconf(_SC_OPEN_MAX);
    return max < INT_MAX ? max : INT_MAX;
}
