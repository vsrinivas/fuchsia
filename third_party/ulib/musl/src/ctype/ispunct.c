#include <ctype.h>

int ispunct(int c) {
    return isgraph(c) && !isalnum(c);
}

int ispunct_l(int c, locale_t l) {
    return ispunct(c);
}
