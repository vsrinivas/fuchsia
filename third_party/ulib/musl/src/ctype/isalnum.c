#include <ctype.h>

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isalnum_l(int c, locale_t l) {
    return isalnum(c);
}
