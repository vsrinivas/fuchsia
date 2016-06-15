#include <fenv.h>
#include <math.h>
#include "libm.h"

/* nearbyint is the same as rint, but it must not raise the inexact exception */

double nearbyint(double x) {
#ifdef FE_INEXACT
    PRAGMA_STDC_FENV_ACCESS_ON
    int e;

    e = fetestexcept(FE_INEXACT);
#endif
    x = rint(x);
#ifdef FE_INEXACT
    if (!e) feclearexcept(FE_INEXACT);
#endif
    return x;
}
