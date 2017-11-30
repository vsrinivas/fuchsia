#include "libc.h"
#include "stdio_impl.h"
#include <stdint.h>
#include <stdlib.h>

_Noreturn void exit(int code) {
    __tls_run_dtors();
    __funcs_on_exit();
    __libc_exit_fini();
    __stdio_exit();
    if (&__libc_extensions_fini != NULL)
        __libc_extensions_fini();
    _Exit(code);
}
