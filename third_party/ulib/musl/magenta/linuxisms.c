#include "debug.h"
#include "syscall.h"

#include <errno.h>
#include <threads.h>

static mtx_t lock;
static const char* log_fn;
static int log_ln;
static syscall_arg_t log_nr;

static void log_linux_syscall(void) {
    warn_unsupported("\nWARNING: %s: %d: Linux Syscalls Not Supported (#%ld)\n", log_fn, log_ln, log_nr);
}

long __linux_syscall(const char* fn, int ln, once_flag* once, syscall_arg_t nr, ...) {
    mtx_lock(&lock);
    log_fn = fn;
    log_ln = ln;
    log_nr = nr;
    call_once(once, log_linux_syscall);
    mtx_unlock(&lock);
    return -ENOSYS;
}
