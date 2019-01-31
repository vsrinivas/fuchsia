#include <inttypes.h>
#include <stdlib.h>

#include "rand48_impl.h"

long nrand48(unsigned short s[3]) {
    return __rand48_step(s, __seed48 + 3) >> 17;
}

long lrand48(void) {
    return nrand48(__seed48);
}
