#include <ctype.h>

int tolower(int c) {
    if (isupper(c))
        return c | 32;
    return c;
}

int tolower_l(int c, locale_t l) {
    return tolower(c);
}
