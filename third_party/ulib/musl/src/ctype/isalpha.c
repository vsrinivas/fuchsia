#include <ctype.h>
#undef isalpha

int isalpha(int c) {
    return ((unsigned)c | 32) - 'a' < 26;
}

int isalpha_l(int c, locale_t l) {
    return isalpha(c);
}
