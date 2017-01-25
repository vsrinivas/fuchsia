#include <ctype.h>
#undef islower

int islower(int c) {
    return (unsigned)c - 'a' < 26;
}

int islower_l(int c, locale_t l) {
    return islower(c);
}
