#include <inttypes.h>
#include <stdlib.h>

#include "rand48_impl.h"

long jrand48(unsigned short s[3]) {
    return __rand48_step(s, __seed48 + 3) >> 16;
}

long mrand48(void) {
    return jrand48(__seed48);
}
