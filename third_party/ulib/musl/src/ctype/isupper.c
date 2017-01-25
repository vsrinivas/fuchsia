#include <ctype.h>
#undef isupper

int isupper(int c) {
    return (unsigned)c - 'A' < 26;
}

int isupper_l(int c, locale_t l) {
    return isupper(c);
}
