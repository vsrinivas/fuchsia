#include "atomic.h"
#include <strings.h>

int ffsl(long i) {
    return i ? a_ctz_l(i) + 1 : 0;
}
