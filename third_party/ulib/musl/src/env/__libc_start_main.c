#include "libc.h"
#include "pthread_impl.h"
#include "tls_impl.h"
#include <elf.h>
#include <stdatomic.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <runtime/message.h>
#include <runtime/processargs.h>
#include <runtime/thread.h>

static void dummy(void) {}
weak_alias(dummy, _init);

__attribute__((__weak__, __visibility__("hidden"))) extern void (*const __init_array_start)(void),
    (*const __init_array_end)(void);

static void dummy1(void* p) {}
weak_alias(dummy1, __init_ssp);

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
void __libc_extensions_init(uint32_t handle_count,
                            mx_handle_t handle[],
                            uint32_t handle_info[]) __attribute__((weak));

// hook to let certain very low level processes muck
// with arg before things start
void* __libc_intercept_arg(void*) __attribute__((weak));

_Noreturn void __libc_start_main(int (*main)(int, char**, char**),
                                 uintptr_t stack_end, void* arg) {
    if (&__libc_intercept_arg != NULL)
        arg = __libc_intercept_arg(arg);

    // extract process startup information from message pipe in arg
    mx_handle_t bootstrap = (uintptr_t)arg;

    uint32_t nbytes, nhandles;
    mx_status_t status = mxr_message_size(bootstrap, &nbytes, &nhandles);
    if (status != NO_ERROR)
        nbytes = nhandles = 0;

    MXR_PROCESSARGS_BUFFER(buffer, nbytes);
    mx_handle_t handles[nhandles];
    mx_proc_args_t* procargs = NULL;
    uint32_t* handle_info = NULL;
    if (status == NO_ERROR)
        status = mxr_processargs_read(bootstrap, buffer, nbytes,
                                      handles, nhandles,
                                      &procargs, &handle_info);

    uint32_t argc = 0;
    uint32_t envc = 0;
    if (status == NO_ERROR) {
        argc = procargs->args_num;
        envc = procargs->environ_num;
    }

    // Use a single contiguous buffer for argv and envp, with two
    // extra words of terminator on the end.  In traditional Unix
    // process startup, the stack contains argv followed immediately
    // by envp and that's followed immediately by the auxiliary vector
    // (auxv), which is in two-word pairs and terminated by zero
    // words.  Some crufty programs might assume some of that layout,
    // and it costs us nothing to stay consistent with it here.
    char* args_and_environ[argc + 1 + envc + 1 + 2];
    char** argv = &args_and_environ[0];
    char** envp = &args_and_environ[argc + 1];
    char** dummy_auxv = &args_and_environ[argc + 1 + envc + 1];
    dummy_auxv[0] = dummy_auxv[1] = 0;

    if (status == NO_ERROR)
        status = mxr_processargs_strings(buffer, nbytes, argv, envp);
    if (status != NO_ERROR) {
        argc = 0;
        argv = envp = NULL;
    }

    // Find the handles we're interested in among what we were given.
    for (uint32_t i = 0; i < nhandles; ++i) {
        switch (MX_HND_INFO_TYPE(handle_info[i])) {
        case MX_HND_TYPE_PROC_SELF:
            // The handle will have been installed already by dynamic
            // linker startup, but now we have another one.  They
            // should of course be handles to the same process, but
            // just for cleanliness switch to the "main" one.
            if (libc.proc != MX_HANDLE_INVALID)
                _mx_handle_close(libc.proc);
            libc.proc = handles[i];
            handles[i] = MX_HANDLE_INVALID;
            handle_info[i] = 0;
            break;

        case MX_HND_TYPE_STACK_VMO:;
            // Assume stack grows down.  The protocol is that our
            // creator mapped the entire stack VMO and then passed
            // us the handle.  We know the top of the stack from the
            // entry point code.  From the VMO we can find the size.
            // We assume (per protocol) that the whole thing is
            // mapped.  Thus we know the bounds of our stack.
            uint64_t stack_vmo_size;
            status = _mx_vmo_get_size(handles[i], &stack_vmo_size);
            if (status == NO_ERROR) {
                libc.stack_size = stack_vmo_size;
                libc.stack_base = stack_end - libc.stack_size;
            }
            // TODO(mcgrathr): Perhaps we should stash this handle
            // somewhere, or close it?  For now we don't have anything
            // else we want it for but maybe there will be something.
            // So leave it to be collected.
            break;
        }
    }

    atomic_fetch_add(&libc.thread_count, 1);
    mxr_thread_t* mxr_thread = __mxr_thread_main();

    __environ = envp;
    __init_tls(mxr_thread);
    // TODO(kulakowski) Set up ssp once kernel randomness exists
    // __init_ssp((void*)aux[AT_RANDOM]);
    __init_security();

    // allow companion libraries a chance to poke at this
    if (&__libc_extensions_init != NULL)
        __libc_extensions_init(nhandles, handles, handle_info);

    __libc_start_init();

    // Pass control to the application
    exit(main(argc, argv, envp));
}
