#define _ALL_SOURCE
#include "libc.h"
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

/* Ensure that at least 32 atexit handlers can be registered without malloc */
#define COUNT 32

static struct fl {
    struct fl* next;
    void (*f[COUNT])(void*);
    void* a[COUNT];
} builtin, *head;

static int slot;
static mtx_t lock = MTX_INIT;

// Phantom unlock to satisfy analysis when actually we leave it locked forever.
__TA_RELEASE(&lock) __TA_NO_THREAD_SAFETY_ANALYSIS
static void synchronize_exit(void) {}

void __funcs_on_exit(void) {
    void (*func)(void*), *arg;
    mtx_lock(&lock);
    for (; head; head = head->next, slot = COUNT) {
        while (slot-- > 0) {
            func = head->f[slot];
            arg = head->a[slot];
            mtx_unlock(&lock);
            func(arg);
            mtx_lock(&lock);
        }
    }

    // Leaving this lock held effectively synchronizes the rest of exit after
    // we return to it.  It's technically undefined behavior for the program
    // to enter exit twice no matter what, so worrying about it at all is just
    // trying to give the most useful possible result for a buggy program.  Up
    // to this point, we gracefully handle multiple threads calling exit by
    // giving them a random interleaving of which thread runs the next atexit
    // hook.  The rest of the teardown that exit does after this is presumed
    // to happen once in a single thread.  So the most graceful way to
    // maintain orderly shutdown in a buggy program is to err on the side of
    // deadlock (if DSO destructors or stdio teardown try to synchronize with
    // another thread that's illegally trying to enter exit again).
    synchronize_exit();
}

void __cxa_finalize(void* dso) {}

int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    mtx_lock(&lock);

    /* Defer initialization of head so it can be in BSS */
    if (!head)
        head = &builtin;

    /* If the current function list is full, add a new one */
    if (slot == COUNT) {
        struct fl* new_fl = calloc(sizeof(struct fl), 1);
        if (!new_fl) {
            mtx_unlock(&lock);
            return -1;
        }
        new_fl->next = head;
        head = new_fl;
        slot = 0;
    }

    /* Append function to the list. */
    head->f[slot] = func;
    head->a[slot] = arg;
    slot++;

    mtx_unlock(&lock);
    return 0;
}

static void call(void* p) {
    ((void (*)(void))(uintptr_t)p)();
}

// In an implementation where dlclose actually unloads a module and runs
// its destructors, the third argument to __cxa_atexit must differ between
// modules (that is, between the main executable and between each DSO) so
// that dlclose can run the subset of destructors registered by that one
// DSO's code.  For C++ static destructors, the compiler generates the call:
//     __cxa_atexit(&destructor, &instance, &__dso_handle);
// __dso_handle is defined with __attribute__((visibility("hidden"))) in
// a special object crtbegin.o that is included implicitly in every link.
// For the C atexit API to do the equivalent, atexit must be defined in
// a small static library that is linked into things that dynamically link
// in -lc; that's the only way for &__dso_handle to refer to the different
// instance of that symbol in each module.
//
// Our dlclose doesn't actually do anything, so we never need to run a
// subset of destructors before we run them all at actual process exit.
// Hence, the third argument to __cxa_atexit is ignored and it doesn't
// matter what we pass it; thus, we can include atexit in the -lc DSO
// as we do here.
int atexit(void (*func)(void)) {
    return __cxa_atexit(call, (void*)(uintptr_t)func, NULL);
}
