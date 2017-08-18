#define _BSD_SOURCE
#include "libc.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define ALIGN (sizeof(size_t) - 1)
#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x)-ONES) & ~(x)&HIGHS)

size_t strlcpy(char* d, const char* s, size_t n) {
    char* d0 = d;

    if (!n--)
        goto finish;
#if !__has_feature(address_sanitizer)
    // This reads past the end of the string, which is usually OK since
    // it won't cross a page boundary.  But under ASan, even one byte
    // past the actual end is diagnosed.
    if (((uintptr_t)s & ALIGN) == ((uintptr_t)d & ALIGN)) {
        for (; ((uintptr_t)s & ALIGN) && n && (*d = *s); n--, s++, d++)
            ;
        if (n && *s) {
            size_t* wd = (void*)d;
            const size_t* ws = (const void*)s;
            for (; n >= sizeof(size_t) && !HASZERO(*ws); n -= sizeof(size_t), ws++, wd++)
                *wd = *ws;
            d = (void*)wd;
            s = (const void*)ws;
        }
    }
#endif
    for (; n && (*d = *s); n--, s++, d++)
        ;
    *d = 0;
finish:
    return d - d0 + strlen(s);
}
