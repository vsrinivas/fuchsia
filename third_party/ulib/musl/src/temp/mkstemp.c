#include "libc.h"
#include <stdlib.h>

int __mkostemps(char*, int, int);

int mkstemp(char* template) {
    return __mkostemps(template, 0, 0);
}

LFS64(mkstemp);
