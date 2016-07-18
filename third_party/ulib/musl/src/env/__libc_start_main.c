#include "libc.h"
#include <elf.h>
#include <string.h>

#include <runtime/process.h>

void __init_tls(void);
void __mxr_thread_main(void);

static void dummy(void) {}
weak_alias(dummy, _init);

__attribute__((__weak__, __visibility__("hidden"))) extern void (*const __init_array_start)(void),
    (*const __init_array_end)(void);

static void dummy1(void* p) {}
weak_alias(dummy1, __init_ssp);

#define AUX_CNT 38

void __init_security(void) {
// TODO(kulakowski) Re-enable this once we have file descriptors up.
#if 0
    if (aux[AT_UID] == aux[AT_EUID] && aux[AT_GID] == aux[AT_EGID] && !aux[AT_SECURE]) return;

    struct pollfd pfd[3] = {{.fd = 0}, {.fd = 1}, {.fd = 2}};
#ifdef SYS_poll
    __syscall(SYS_poll, pfd, 3, 0);
#else
    __syscall(SYS_ppoll, pfd, 3, &(struct timespec){0}, 0, _NSIG / 8);
#endif
    for (i = 0; i < 3; i++)
        if (pfd[i].revents & POLLNVAL)
            if (__sys_open("/dev/null", O_RDWR) < 0) a_crash();
    libc.secure = 1;
#endif
}

static void libc_start_init(void) {
    _init();
    uintptr_t a = (uintptr_t)&__init_array_start;
    for (; a < (uintptr_t)&__init_array_end; a += sizeof(void (*)(void)))
        (*(void (**)(void))a)();
}
weak_alias(libc_start_init, __libc_start_init);

// hook for extension libraries to init
static void libc_extensions_init(mx_proc_info_t* pi) {}
weak_alias(libc_extensions_init, __libc_extensions_init);

// hook to let certain very low level processes muck
// with arg before things start
static void* libc_intercept_arg(void* arg) {
    return arg;
}
weak_alias(libc_intercept_arg, __libc_intercept_arg);

int __libc_start_main(int (*main)(int, char**, char**), void* arg) {
    arg = __libc_intercept_arg(arg);

    // extract process startup information from message pipe in arg
    mx_proc_info_t* pi = mxr_process_parse_args(arg);

    __mxr_thread_main();

    // TODO(kulakowski) Set up environment
    __init_tls();
    // TODO(kulakowski) Set up ssp once kernel randomness exists
    // __init_ssp((void*)aux[AT_RANDOM]);
    __init_security();

    // allow companion libraries a chance to poke at this
    __libc_extensions_init(pi);
    __libc_start_init();

    // Pass control to the application
    exit(main(pi->argc, pi->argv, NULL));
}
