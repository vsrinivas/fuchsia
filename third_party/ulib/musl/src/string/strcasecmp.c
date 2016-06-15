#include "libc.h"
#include <ctype.h>
#include <strings.h>

int strcasecmp(const char* _l, const char* _r) {
    const unsigned char *l = (void *)_l, *r = (void *)_r;
    for (; *l && *r && (*l == *r || tolower(*l) == tolower(*r)); l++, r++)
        ;
    return tolower(*l) - tolower(*r);
}

int __strcasecmp_l(const char* l, const char* r, locale_t loc) {
    return strcasecmp(l, r);
}

weak_alias(__strcasecmp_l, strcasecmp_l);
