#include <ctype.h>

int isblank(int c) {
    return (c == ' ' || c == '\t');
}

int isblank_l(int c, locale_t l) {
    return isblank(c);
}
