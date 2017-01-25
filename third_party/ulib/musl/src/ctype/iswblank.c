#include <ctype.h>
#include <wctype.h>

int iswblank(wint_t wc) {
    return isblank(wc);
}

int iswblank_l(wint_t c, locale_t l) {
    return iswblank(c);
}
