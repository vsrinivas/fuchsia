#include "libc.h"
#include <stdlib.h>

static void dummy(void) {}
weak_alias(dummy, __funcs_on_quick_exit);

_Noreturn void quick_exit(int code) {
    __funcs_on_quick_exit();
    _Exit(code);
}
