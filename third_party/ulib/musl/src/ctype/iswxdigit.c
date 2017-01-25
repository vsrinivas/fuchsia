#include <wctype.h>

int iswxdigit(wint_t wc) {
    return (unsigned)(wc - '0') < 10 || (unsigned)((wc | 32) - 'a') < 6;
}

int iswxdigit_l(wint_t c, locale_t l) {
    return iswxdigit(c);
}
