#include "libm.h"
#include <fenv.h>
#include <math.h>

float nearbyintf(float x) {
#ifdef FE_INEXACT
    PRAGMA_STDC_FENV_ACCESS_ON
    int e;

    e = fetestexcept(FE_INEXACT);
#endif
    x = rintf(x);
#ifdef FE_INEXACT
    if (!e)
        feclearexcept(FE_INEXACT);
#endif
    return x;
}
