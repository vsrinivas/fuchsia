#include <string.h>

#include <limits.h>
#include <magenta/compiler.h>
#include <stdint.h>

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x)-ONES) & ~(x)&HIGHS)

size_t strlen(const char* s) {
    const char* a = s;
    for (; (uintptr_t)s % ALIGN; s++)
        if (!*s)
            return s - a;
    const size_t* w = (const void*)s;
#if !__has_feature(address_sanitizer)
    // This reads past the end of the string, which is usually OK since it
    // won't cross a page boundary.  But under ASan, even one byte past the
    // actual end is diagnosed.
    while (!HASZERO(*w))
        ++w;
#endif
    for (s = (const void*)w; *s; s++)
        ;
    return s - a;
}
