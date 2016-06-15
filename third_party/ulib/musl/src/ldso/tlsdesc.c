#include "libc.h"
#include <stddef.h>

__attribute__((__visibility__("hidden"))) ptrdiff_t __tlsdesc_static(void), __tlsdesc_dynamic(void);

ptrdiff_t __tlsdesc_static(void) {
    return 0;
}

weak_alias(__tlsdesc_static, __tlsdesc_dynamic);
