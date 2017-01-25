#include <ctype.h>
#undef isspace

int isspace(int c) {
    return c == ' ' || (unsigned)c - '\t' < 5;
}

int isspace_l(int c, locale_t l) {
    return isspace(c);
}
