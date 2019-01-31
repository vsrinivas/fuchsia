#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double remquol(long double x, long double y, int* quo) {
    return remquo(x, y, quo);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
long double remquol(long double x, long double y, int* quo) {
    union ldshape ux = {x}, uy = {y};
    int ex = ux.i.se & 0x7fff;
    int ey = uy.i.se & 0x7fff;
    int sx = ux.i.se >> 15;
    int sy = uy.i.se >> 15;
    uint32_t q;

    *quo = 0;
    if (y == 0 || isnan(y) || ex == 0x7fff)
        return (x * y) / (x * y);
    if (x == 0)
        return x;

    /* normalize x and y */
    if (!ex) {
        ux.i.se = ex;
        ux.f *= 0x1p120f;
        ex = ux.i.se - 120;
    }
    if (!ey) {
        uy.i.se = ey;
        uy.f *= 0x1p120f;
        ey = uy.i.se - 120;
    }

    q = 0;
    if (ex >= ey) {
/* x mod y */
#if LDBL_MANT_DIG == 64
        uint64_t i, zx, my;
        zx = ux.i.m;
        my = uy.i.m;
        for (; ex > ey; ex--) {
            i = zx - my;
            if (zx >= my) {
                zx = 2 * i;
                q++;
                q <<= 1;
            } else if (2 * zx < zx) {
                zx = 2 * zx - my;
                q <<= 1;
                q++;
            } else {
                zx = 2 * zx;
                q <<= 1;
            }
        }
        i = zx - my;
        if (zx >= my) {
            zx = i;
            q++;
        }
        if (zx == 0)
            ex = -120;
        else
            for (; zx >> 63 == 0; zx *= 2, ex--)
                ;
        ux.i.m = zx;
#elif LDBL_MANT_DIG == 113
        uint64_t hi, lo, xhi, xlo, yhi, ylo;
        xhi = (ux.i2.hi & -1ULL >> 16) | 1ULL << 48;
        yhi = (uy.i2.hi & -1ULL >> 16) | 1ULL << 48;
        xlo = ux.i2.lo;
        ylo = ux.i2.lo;
        for (; ex > ey; ex--) {
            hi = xhi - yhi;
            lo = xlo - ylo;
            if (xlo < ylo)
                hi -= 1;
            if (hi >> 63 == 0) {
                xhi = 2 * hi + (lo >> 63);
                xlo = 2 * lo;
                q++;
            } else {
                xhi = 2 * xhi + (xlo >> 63);
                xlo = 2 * xlo;
            }
            q <<= 1;
        }
        hi = xhi - yhi;
        lo = xlo - ylo;
        if (xlo < ylo)
            hi -= 1;
        if (hi >> 63 == 0) {
            xhi = hi;
            xlo = lo;
            q++;
        }
        if ((xhi | xlo) == 0)
            ex = -120;
        else
            for (; xhi >> 48 == 0; xhi = 2 * xhi + (xlo >> 63), xlo = 2 * xlo, ex--)
                ;
        ux.i2.hi = xhi;
        ux.i2.lo = xlo;
#endif
    }

    /* scale result and decide between |x| and |x|-|y| */
    if (ex <= 0) {
        ux.i.se = ex + 120;
        ux.f *= 0x1p-120f;
    } else
        ux.i.se = ex;
    x = ux.f;
    if (sy)
        y = -y;
    if (ex == ey || (ex + 1 == ey && (2 * x > y || (2 * x == y && q % 2)))) {
        x -= y;
        q++;
    }
    q &= 0x7fffffff;
    *quo = sx ^ sy ? -(int)q : (int)q;
    return sx ? -x : x;
}
#endif
