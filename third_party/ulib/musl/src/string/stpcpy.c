#include "libc.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x)-ONES) & ~(x)&HIGHS)

char* __stpcpy(char* restrict d, const char* restrict s) {
#if !__has_feature(address_sanitizer)
    // This reads past the end of the string, which is usually OK since
    // it won't cross a page boundary.  But under ASan, even one byte
    // past the actual end is diagnosed.
    if ((uintptr_t)s % ALIGN == (uintptr_t)d % ALIGN) {
        for (; (uintptr_t)s % ALIGN; s++, d++)
            if (!(*d = *s))
                return d;
        size_t* wd = (void*)d;
        const size_t* ws = (const void*)s;
        for (; !HASZERO(*ws); *wd++ = *ws++)
            ;
        d = (void*)wd;
        s = (const void*)ws;
    }
#endif
    for (; (*d = *s); s++, d++)
        ;

    return d;
}

weak_alias(__stpcpy, stpcpy);
