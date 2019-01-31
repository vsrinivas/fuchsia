#include "libc.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x)-ONES) & ~(x)&HIGHS)

char* __strchrnul(const char* s, int c) {
    c = (unsigned char)c;
    if (!c)
        return (char*)s + strlen(s);

    for (; (uintptr_t)s % ALIGN; s++)
        if (!*s || *(unsigned char*)s == c)
            return (char*)s;
    const size_t* w = (const void*)s;
#if !__has_feature(address_sanitizer)
    // This reads past the end of the string, which is usually OK since it
    // won't cross a page boundary.  But under ASan, even one byte past the
    // actual end is diagnosed.
    size_t k = ONES * c;
    while (!HASZERO(*w) && !HASZERO(*w ^ k))
        ++w;
#endif
    for (s = (const void*)w; *s && *(unsigned char*)s != c; s++)
        ;
    return (char*)s;
}

weak_alias(__strchrnul, strchrnul);
