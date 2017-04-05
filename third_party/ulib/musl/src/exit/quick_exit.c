#include "libc.h"
#include <stdlib.h>

_Noreturn void quick_exit(int code) {
    __funcs_on_quick_exit();
    _Exit(code);
}
