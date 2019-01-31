#pragma once

#include "libc.h"

/* shared by acosl, asinl and atan2l */
#define pio2_hi __pio2_hi
#define pio2_lo __pio2_lo
ATTR_LIBC_VISIBILITY extern const long double pio2_hi, pio2_lo;

long double __invtrigl_R(long double z);
