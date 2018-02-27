#include "libc.h"
#include "threads_impl.h"
#include <stdint.h>
#include <string.h>

uintptr_t __stack_chk_guard;

void __stack_chk_fail(void) {
    __builtin_trap();
}

__attribute__((__visibility__("hidden"))) void __stack_chk_fail_local(void);

weak_alias(__stack_chk_fail, __stack_chk_fail_local);
