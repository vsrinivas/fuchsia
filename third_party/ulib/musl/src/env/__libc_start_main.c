#include "libc.h"
#include "magenta_impl.h"
#include "pthread_impl.h"
#include "setjmp_impl.h"
#include <elf.h>
#include <stdatomic.h>
#include <string.h>

#include <magenta/sanitizer.h>
#include <magenta/syscalls.h>
#include <runtime/message.h>
#include <runtime/processargs.h>
#include <runtime/thread.h>

// Hook for extension libraries to init. Extensions must zero out
// handle[i] and handle_info[i] for any handles they claim.
void __libc_extensions_init(uint32_t handle_count,
                            mx_handle_t handle[],
                            uint32_t handle_info[],
                            uint32_t name_count,
                            char** names) __attribute__((weak));

struct start_params {
    uint32_t argc, nhandles, namec;
    char** argv;
    char** names;
    mx_handle_t* handles;
    uint32_t* handle_info;
    int (*main)(int, char**, char**);
    pthread_t td;
};

// This gets called via inline assembly below, after switching onto
// the newly-allocated (safe) stack.
static _Noreturn void start_main(const struct start_params*)
    __asm__("start_main") __attribute__((used));
static void start_main(const struct start_params* p) {
    __sanitizer_startup_hook(p->argc, p->argv, __environ,
                             p->td->safe_stack.iov_base,
                             p->td->safe_stack.iov_len);

    // Allow companion libraries a chance to claim handles, zeroing out
    // handles[i] and handle_info[i] for handles they claim.
    if (&__libc_extensions_init != NULL)
        __libc_extensions_init(p->nhandles, p->handles, p->handle_info,
                               p->namec, p->names);

    // Give any unclaimed handles to mx_get_startup_handle(). This function
    // takes ownership of the data, but not the memory: it assumes that the
    // arrays are valid as long as the process is alive.
    __libc_startup_handles_init(p->nhandles, p->handles, p->handle_info);

    // Run static constructors et al.
    __libc_start_init();

    // Pass control to the application.
    exit((*p->main)(p->argc, p->argv, __environ));
}

__NO_SAFESTACK _Noreturn void __libc_start_main(
    void* arg, int (*main)(int, char**, char**)) {

    // Initialize stack-protector canary value first thing.  Do the setjmp
    // manglers in the same call to avoid the overhead of two system calls.
    // That means we need a temporary buffer on the stack, which we then
    // want to clear out so the values don't leak there.
    size_t actual;
    struct randoms {
        uintptr_t stack_guard;
        struct setjmp_manglers setjmp_manglers;
    } randoms;
    static_assert(sizeof(randoms) <= MX_CPRNG_DRAW_MAX_LEN, "");
    mx_status_t status = _mx_cprng_draw(&randoms, sizeof(randoms), &actual);
    if (status != MX_OK || actual != sizeof(randoms))
        __builtin_trap();
    __stack_chk_guard = randoms.stack_guard;
    __setjmp_manglers = randoms.setjmp_manglers;
    // Zero the stack temporaries.
    randoms = (struct randoms) {};
    // Tell the compiler that the value is used, so it doesn't optimize
    // out the zeroing as dead stores.
    __asm__("# keepalive %0" :: "m"(randoms));

    // extract process startup information from channel in arg
    mx_handle_t bootstrap = (uintptr_t)arg;

    struct start_params p = { .main = main };
    uint32_t nbytes;
    status = mxr_message_size(bootstrap, &nbytes, &p.nhandles);
    if (status != MX_OK)
        nbytes = p.nhandles = 0;

    MXR_PROCESSARGS_BUFFER(buffer, nbytes);
    mx_handle_t handles[p.nhandles];
    p.handles = handles;
    mx_proc_args_t* procargs = NULL;
    if (status == MX_OK)
        status = mxr_processargs_read(bootstrap, buffer, nbytes,
                                      handles, p.nhandles,
                                      &procargs, &p.handle_info);

    uint32_t envc = 0;
    if (status == MX_OK) {
        p.argc = procargs->args_num;
        envc = procargs->environ_num;
        p.namec = procargs->names_num;
    }

    // Use a single contiguous buffer for argv and envp, with two
    // extra words of terminator on the end.  In traditional Unix
    // process startup, the stack contains argv followed immediately
    // by envp and that's followed immediately by the auxiliary vector
    // (auxv), which is in two-word pairs and terminated by zero
    // words.  Some crufty programs might assume some of that layout,
    // and it costs us nothing to stay consistent with it here.
    char* args_and_environ[p.argc + 1 + envc + 1 + 2];
    p.argv = &args_and_environ[0];
    __environ = &args_and_environ[p.argc + 1];
    char** dummy_auxv = &args_and_environ[p.argc + 1 + envc + 1];
    dummy_auxv[0] = dummy_auxv[1] = 0;

    char* names[p.namec + 1];
    p.names = names;

    if (status == MX_OK)
        status = mxr_processargs_strings(buffer, nbytes, p.argv, __environ, p.names);
    if (status != MX_OK) {
        p.argc = 0;
        p.argv = __environ = NULL;
        p.namec = 0;
    }

    // Find the handles we're interested in among what we were given.
    mx_handle_t main_thread_handle = MX_HANDLE_INVALID;
    for (uint32_t i = 0; i < p.nhandles; ++i) {
        switch (PA_HND_TYPE(p.handle_info[i])) {
        case PA_PROC_SELF:
            // The handle will have been installed already by dynamic
            // linker startup, but now we have another one.  They
            // should of course be handles to the same process, but
            // just for cleanliness switch to the "main" one.
            if (__magenta_process_self != MX_HANDLE_INVALID)
                _mx_handle_close(__magenta_process_self);
            __magenta_process_self = handles[i];
            handles[i] = MX_HANDLE_INVALID;
            p.handle_info[i] = 0;
            break;

        case PA_JOB_DEFAULT:
            // The default job provided to the process to use for
            // creation of additional processes.  It may or may not
            // be the job this process is a child of.  It may not
            // be provided at all.
            if (__magenta_job_default != MX_HANDLE_INVALID)
                _mx_handle_close(__magenta_job_default);
            __magenta_job_default = handles[i];
            handles[i] = MX_HANDLE_INVALID;
            p.handle_info[i] = 0;
            break;

        case PA_VMAR_ROOT:
            // As above for PROC_SELF
            if (__magenta_vmar_root_self != MX_HANDLE_INVALID)
                _mx_handle_close(__magenta_vmar_root_self);
            __magenta_vmar_root_self = handles[i];
            handles[i] = MX_HANDLE_INVALID;
            p.handle_info[i] = 0;
            break;

        case PA_THREAD_SELF:
            main_thread_handle = handles[i];
            handles[i] = MX_HANDLE_INVALID;
            p.handle_info[i] = 0;
            break;
        }
    }

    atomic_store(&libc.thread_count, 1);

    // This consumes the thread handle and sets up the thread pointer.
    p.td = __init_main_thread(main_thread_handle);

    // Switch to the allocated stack and call start_main(&p) there.
    // The original stack stays around just to hold argv et al.
    // The new stack is whole pages, so it's sufficiently aligned.

#ifdef __x86_64__
    // The x86-64 ABI requires %rsp % 16 = 8 on entry.  The zero word
    // at (%rsp) serves as the return address for the outermost frame.
    __asm__("lea -8(%[base], %[len], 1), %%rsp\n"
            "jmp start_main\n"
            "# Target receives %[arg]" : :
            [base]"r"(p.td->safe_stack.iov_base),
            [len]"r"(p.td->safe_stack.iov_len),
            "m"(p), // Tell the compiler p's fields are all still alive.
            [arg]"D"(&p));
#elif defined(__aarch64__)
    __asm__("add sp, %[base], %[len]\n"
            "mov x0, %[arg]\n"
            "b start_main" : :
            [base]"r"(p.td->safe_stack.iov_base),
            [len]"r"(p.td->safe_stack.iov_len),
            "m"(p), // Tell the compiler p's fields are all still alive.
            [arg]"r"(&p));
#else
#error what architecture?
#endif

    __builtin_unreachable();
}
