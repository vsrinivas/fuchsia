#include <string.h>

#include <limits.h>
#include <magenta/compiler.h>
#include <stdint.h>

#define SS (sizeof(size_t))
#define ALIGN (sizeof(size_t) - 1)
#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x)-ONES) & ~(x)&HIGHS)

void* memchr(const void* src, int c, size_t n) {
    const unsigned char* s = src;
    c = (unsigned char)c;
    for (; ((uintptr_t)s & ALIGN) && n && *s != c; s++, n--)
        ;
    if (n && *s != c) {
        const size_t* w = (const void*)s;
#if !__has_feature(address_sanitizer)
        // This reads past the end of the string, which is usually OK since
        // it won't cross a page boundary.  But under ASan, even one byte
        // past the actual end is diagnosed.
        size_t k = ONES * c;
        while (n >= SS && !HASZERO(*w ^ k)) {
            ++w;
            n -= SS;
        }
#endif
        for (s = (const void*)w; n && *s != c; s++, n--)
            ;
    }
    return n ? (void*)s : 0;
}
