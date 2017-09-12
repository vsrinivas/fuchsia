#define _GNU_SOURCE
#include <stdlib.h>

int getloadavg(double* a, int n) {
    if (n <= 0)
        return n ? -1 : 0;
    if (n > 3)
        n = 3;
    // TODO(kulakowski) Ask zircon for load average.
    return -1;
}
