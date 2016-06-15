#include "libc.h"
#include <string.h>

void __aeabi_memset(void* dest, size_t n, int c) {
    memset(dest, c, n);
}
weak_alias(__aeabi_memset, __aeabi_memset4);
weak_alias(__aeabi_memset, __aeabi_memset8);
