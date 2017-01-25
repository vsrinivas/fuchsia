#include <wctype.h>

int iswlower(wint_t wc) {
    return towupper(wc) != wc;
}

int iswlower_l(wint_t c, locale_t l) {
    return iswlower(c);
}
