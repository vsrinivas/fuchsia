#include <string.h>

#include <limits.h>
#include <magenta/compiler.h>
#include <stdint.h>

#define ALIGN (sizeof(size_t) - 1)
#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x)-ONES) & ~(x)&HIGHS)

void* memccpy(void* restrict dest, const void* restrict src, int c, size_t n) {
    unsigned char* d = dest;
    const unsigned char* s = src;

    c = (unsigned char)c;
#if !__has_feature(address_sanitizer)
    // This reads past the end of the string, which is usually OK since
    // it won't cross a page boundary.  But under ASan, even one byte
    // past the actual end is diagnosed.
    if (((uintptr_t)s & ALIGN) == ((uintptr_t)d & ALIGN)) {
        for (; ((uintptr_t)s & ALIGN) && n && (*d = *s) != c; n--, s++, d++)
            ;
        if ((uintptr_t)s & ALIGN)
            goto tail;
        size_t k = ONES * c;
        size_t* wd = (void*)d;
        const size_t* ws = (const void*)s;
        for (; n >= sizeof(size_t) && !HASZERO(*ws ^ k); n -= sizeof(size_t), ws++, wd++)
            *wd = *ws;
        d = (void*)wd;
        s = (const void*)ws;
    }
#endif
    for (; n && (*d = *s) != c; n--, s++, d++)
        ;
tail:
    if (*s == c)
        return d + 1;
    return 0;
}
