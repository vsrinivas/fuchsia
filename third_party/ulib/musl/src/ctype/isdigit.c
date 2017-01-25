#include <ctype.h>
#undef isdigit

int isdigit(int c) {
    return (unsigned)c - '0' < 10;
}

int isdigit_l(int c, locale_t l) {
    return isdigit(c);
}
