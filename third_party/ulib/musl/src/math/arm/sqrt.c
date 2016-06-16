#include <math.h>

#if __ARM_PCS_VFP || (__VFP_FP__ && !__SOFTFP__)

double sqrt(double x) {
    __asm__("vsqrt.f64 %P0, %P1" : "=w"(x) : "w"(x));
    return x;
}

#else

#include "../../../third_party/math/sqrt.c"

#endif
