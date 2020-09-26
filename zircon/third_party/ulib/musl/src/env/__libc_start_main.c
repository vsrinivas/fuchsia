#include <elf.h>
#include <lib/processargs/processargs.h>
#include <stdatomic.h>
#include <string.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>
#include <zircon/utc.h>

#include <runtime/thread.h>

#include "libc.h"
#include "setjmp_impl.h"
#include "threads_impl.h"
#include "zircon_impl.h"

struct start_params {
  int (*main)(int, char**, char**);
  thrd_t td;
  uint8_t* buffer;
  zx_proc_args_t* procargs;
  zx_handle_t* handles;
  uint32_t* handle_info;
  uint32_t nbytes, nhandles;
  zx_handle_t utc_reference;
};

// See dynlink.c for the full explanation.  The compiler generates calls to
// these implicitly.  They are PLT calls into the ASan runtime, which is fine
// in and of itself at this point (unlike in dynlink.c).  But they might also
// use ShadowCallStack, which is not set up yet.  So make sure references here
// only use the libc-internal symbols, which don't have any setup requirements.
#if __has_feature(address_sanitizer)
__asm__(".weakref __asan_memcpy,__libc_memcpy");
__asm__(".weakref __asan_memset,__libc_memset");
#endif

// This gets called via inline assembly below, after switching onto
// the newly-allocated (safe) stack.
static _Noreturn void start_main(const struct start_params*) __asm__("start_main")
    __attribute__((used));
static void start_main(const struct start_params* p) {
  uint32_t argc = p->procargs->args_num;
  uint32_t envc = p->procargs->environ_num;
  uint32_t namec = p->procargs->names_num;

  // Now that it is safe to call safe-stack enabled functions, go ahead and install the UTC
  // reference clock, if one was provided to us.
  if (p->utc_reference != ZX_HANDLE_INVALID) {
    zx_handle_t old_clock = ZX_HANDLE_INVALID;

    // Success or fail, libc has consumed our clock handle.  It no longer
    // belongs to us.  From here on out, it is very important that nothing
    // attempts to make use of p->utc_reference.
    _zx_utc_reference_swap(p->utc_reference, &old_clock);

    // If there had been a clock previously, we now own it, but have no use for
    // it.  Simply close it.
    if (old_clock != ZX_HANDLE_INVALID) {
      _zx_handle_close(old_clock);
    }
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
  __environ = &args_and_environ[argc + 1];
  char** dummy_auxv = &args_and_environ[argc + 1 + envc + 1];
  dummy_auxv[0] = dummy_auxv[1] = 0;

  char* names[namec + 1];
  zx_status_t status = processargs_strings(p->buffer, p->nbytes, argv, __environ, names);
  if (status != ZX_OK) {
    argc = namec = 0;
    argv = __environ = NULL;
  }

  for (uint32_t n = 0; n < p->nhandles; n++) {
    unsigned arg = PA_HND_ARG(p->handle_info[n]);
    zx_handle_t h = p->handles[n];

    switch (PA_HND_TYPE(p->handle_info[n])) {
      case PA_NS_DIR: {
        // Avoid strcmp, because it may be instrumented, and we haven't
        // initialized the sanitizer runtime yet.
        const char* name = names[arg];
        if (name[0] == '/' && name[1] == 's' && name[2] == 'v' && name[3] == 'c' && name[4] == 0) {
          // TODO(phosek): We should ideally duplicate the handle since
          // higher layers might consume it and we want to have a guarantee
          // that it stays alive, but that's typically possible since
          // channel handles don't have ZX_RIGHT_DUPLICATE right.
          //
          // TODO(phosek): What if the program uses bind to replace its
          // /svc, should the subsequent invocations to __sanitizer_*
          // use the startup value or reflect the live changes?
          __zircon_namespace_svc = h;
        }
        continue;
      }
    }
  }

  __sanitizer_startup_hook(argc, argv, __environ, p->td->safe_stack.iov_base,
                           p->td->safe_stack.iov_len);

  // Allow companion libraries a chance to claim handles, zeroing out
  // handles[i] and handle_info[i] for handles they claim.
  if (&__libc_extensions_init != NULL) {
    __libc_extensions_init(p->nhandles, p->handles, p->handle_info, namec, names);
  }

  // Give any unclaimed handles to zx_take_startup_handle(). This function
  // takes ownership of the data, but not the memory: it assumes that the
  // arrays are valid as long as the process is alive.
  __libc_startup_handles_init(p->nhandles, p->handles, p->handle_info);

  // Run static constructors et al.
  __libc_start_init();

  // Pass control to the application.
  exit((*p->main)(argc, argv, __environ));
}

NO_ASAN __NO_SAFESTACK _Noreturn void __libc_start_main(zx_handle_t bootstrap,
                                                        int (*main)(int, char**, char**)) {
  // Initialize stack-protector canary value first thing.  Do the setjmp
  // manglers in the same call to avoid the overhead of two system calls.
  // That means we need a temporary buffer on the stack, which we then
  // want to clear out so the values don't leak there.
  struct randoms {
    uintptr_t stack_guard;
    struct setjmp_manglers setjmp_manglers;
  } randoms;
  static_assert(sizeof(randoms) <= ZX_CPRNG_DRAW_MAX_LEN, "");
  _zx_cprng_draw(&randoms, sizeof(randoms));
  __stack_chk_guard = randoms.stack_guard;
  __setjmp_manglers = randoms.setjmp_manglers;
  // Zero the stack temporaries.
  randoms = (struct randoms){};
  // Tell the compiler that the value is used, so it doesn't optimize
  // out the zeroing as dead stores.
  __asm__("# keepalive %0" ::"m"(randoms));

  // extract process startup information from channel in arg
  struct start_params p = {.main = main, .utc_reference = ZX_HANDLE_INVALID};
  zx_status_t status = processargs_message_size(bootstrap, &p.nbytes, &p.nhandles);

  // TODO(44088): Right now, we _always_ expect to receive at least some handles
  // and some bytes in the initial startup message.  Make sure that we have both
  // so that we do not accidentally end up declaring a 0-length VLA on the stack
  // (which is UDB in C11).  See the bug referenced in the TODO, however.  We do
  // not currently formally state that this is a requirement for starting a
  // process, nor do we declare a maximum number of handles which can be sent
  // during startup.  Restructuring and formalizing the process-args startup
  // protocol could help with this situation.
  if ((status == ZX_OK) && p.nbytes && p.nhandles) {
    PROCESSARGS_BUFFER(buffer, p.nbytes);
    zx_handle_t handles[p.nhandles];
    p.buffer = buffer;
    p.handles = handles;
    if (status == ZX_OK) {
      status = processargs_read(bootstrap, buffer, p.nbytes, handles, p.nhandles, &p.procargs,
                                &p.handle_info);
    }
    zx_handle_t main_thread_handle = ZX_HANDLE_INVALID;
    processargs_extract_handles(p.nhandles, handles, p.handle_info, &__zircon_process_self,
                                &__zircon_job_default, &__zircon_vmar_root_self,
                                &main_thread_handle, &p.utc_reference);

    atomic_store(&libc.thread_count, 1);

    // This consumes the thread handle and sets up the thread pointer.
    p.td = __init_main_thread(main_thread_handle);

    // Switch to the allocated stack and call start_main(&p) there.  The
    // original stack stays around just to hold the message buffer and handles
    // array.  The new stack is whole pages, so it's sufficiently aligned.

#ifdef __x86_64__
    // The x86-64 ABI requires %rsp % 16 = 8 on entry.  The zero word
    // at (%rsp) serves as the return address for the outermost frame.
    __asm__(
        "lea -8(%[base], %[len], 1), %%rsp\n"
        "jmp start_main\n"
        "# Target receives %[arg]"
        :
        : [ base ] "r"(p.td->safe_stack.iov_base), [ len ] "r"(p.td->safe_stack.iov_len),
          "m"(p),  // Tell the compiler p's fields are all still alive.
          [ arg ] "D"(&p));
#elif defined(__aarch64__)
    __asm__(
        "add sp, %[base], %[len]\n"
        "mov x18, %[shadow_call_stack]\n"
        // Neither sp nor x18 might be used as an input operand, but x0 might be.
        // So clobber x0 last.  We don't need to declare it to the compiler as a
        // clobber since we'll never come back and it's fine if it's used as an
        // input operand.
        "mov x0, %[arg]\n"
        "b start_main"
        :
        : [ base ] "r"(p.td->safe_stack.iov_base), [ len ] "r"(p.td->safe_stack.iov_len),
          // Shadow call stack grows up.
          [ shadow_call_stack ] "r"(p.td->shadow_call_stack.iov_base),
          "m"(p),  // Tell the compiler p's fields are all still alive.
          [ arg ] "r"(&p));
#else
#error what architecture?
#endif
  }

  __builtin_unreachable();
}
