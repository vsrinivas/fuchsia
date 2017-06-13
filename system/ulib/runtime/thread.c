// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/thread.h>

#include <magenta/stack.h>
#include <magenta/syscalls.h>
#include <runtime/mutex.h>
#include <stddef.h>
#include <stdint.h>

// An mxr_thread_t starts its life JOINABLE.
// - If someone calls mxr_thread_join on it, it transitions to JOINED.
// - If someone calls mxr_thread_detach on it, it transitions to DETACHED.
// - When it exits, it transitions to DONE.
// No other transitions occur.
enum {
    JOINABLE,
    DETACHED,
    JOINED,
    DONE,
};

mx_status_t mxr_thread_destroy(mxr_thread_t* thread) {
    mx_handle_t handle = thread->handle;
    thread->handle = MX_HANDLE_INVALID;
    return handle == MX_HANDLE_INVALID ? MX_OK : _mx_handle_close(handle);
}

// Put the thread into DONE state.  As soon as thread->state has changed to
// to DONE, a caller of mxr_thread_join might complete and deallocate the
// memory containing the thread descriptor.  Hence it's no longer safe to
// touch *thread or read anything out of it.  Therefore we must extract the
// thread handle beforehand.
static int begin_exit(mxr_thread_t* thread, mx_handle_t* out_handle) {
    *out_handle = thread->handle;
    thread->handle = MX_HANDLE_INVALID;
    return atomic_exchange_explicit(&thread->state, DONE, memory_order_release);
}

static _Noreturn void exit_joinable(mx_handle_t handle) {
    // A later mxr_thread_join call will complete immediately.
    if (_mx_handle_close(handle) != MX_OK)
        __builtin_trap();
    // If there were no other handles to the thread, closing the handle
    // killed us right there.  If there are other handles, exit now.
    _mx_thread_exit();
}

static _Noreturn void exit_joined(mxr_thread_t* thread, mx_handle_t handle) {
    // Wake the _mx_futex_wait in mxr_thread_join (below), and then die.
    // This has to be done with the special three-in-one vDSO call because
    // as soon as the mx_futex_wake completes, the joiner is free to unmap
    // our stack out from under us.  Doing so is a benign race: if the
    // address is unmapped and our futex_wake fails, it's OK; if the memory
    // is reused for something else and our futex_wake tickles somebody
    // completely unrelated, well, that's why futex_wait can always have
    // spurious wakeups.
    _mx_futex_wake_handle_close_thread_exit(&thread->state, 1, handle);
    __builtin_trap();
}

static _Noreturn void thread_trampoline(uintptr_t ctx) {
    mxr_thread_t* thread = (mxr_thread_t*)ctx;

    thread->entry(thread->arg);

    mx_handle_t handle;
    int old_state = begin_exit(thread, &handle);
    switch (old_state) {
    case DETACHED:
        // Nobody cares.  Just die, alone and in the dark.
        // Fall through.

    case JOINABLE:
        // Nobody's watching right now, but they might care later.
        exit_joinable(handle);
        break;

    case JOINED:
        // Somebody loves us!  Or at least intends to inherit when we die.
        exit_joined(thread, handle);
        break;
    }

    __builtin_trap();
}

_Noreturn void mxr_thread_exit_unmap_if_detached(
    mxr_thread_t* thread, mx_handle_t vmar, uintptr_t addr, size_t len) {

    mx_handle_t handle;
    int old_state = begin_exit(thread, &handle);
    switch (old_state) {
    case DETACHED:
        // Don't bother touching the mxr_thread_t about to be unmapped.
        _mx_vmar_unmap_handle_close_thread_exit(vmar, addr, len, handle);
        // If that returned, the unmap operation was invalid.
        break;

    case JOINABLE:
        exit_joinable(handle);
        break;

    case JOINED:
        exit_joined(thread, handle);
        break;
    }

    __builtin_trap();
}

// Local implementation so libruntime does not depend on libc.
static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (*s++ != '\0')
        ++len;
    return len;
}

static void initialize_thread(mxr_thread_t* thread,
                              mx_handle_t handle, bool detached) {
    *thread = (mxr_thread_t){
        .handle = handle,
        .state = ATOMIC_VAR_INIT(detached ? DETACHED : JOINABLE),
    };
}

mx_status_t mxr_thread_create(mx_handle_t process, const char* name,
                              bool detached, mxr_thread_t* thread) {
    initialize_thread(thread, MX_HANDLE_INVALID, detached);
    if (name == NULL)
        name = "";
    size_t name_length = local_strlen(name) + 1;
    return _mx_thread_create(process, name, name_length, 0, &thread->handle);
}

mx_status_t mxr_thread_start(mxr_thread_t* thread, uintptr_t stack_addr, size_t stack_size, mxr_thread_entry_t entry, void* arg) {
    thread->entry = entry;
    thread->arg = arg;

    // compute the starting address of the stack
    uintptr_t sp = compute_initial_stack_pointer(stack_addr, stack_size);

    // kick off the new thread
    mx_status_t status = _mx_thread_start(thread->handle,
                                          (uintptr_t)thread_trampoline, sp,
                                          (uintptr_t)thread, 0);

    if (status != MX_OK)
        mxr_thread_destroy(thread);
    return status;
}

mx_status_t mxr_thread_join(mxr_thread_t* thread) {
    int old_state = JOINABLE;
    if (atomic_compare_exchange_strong_explicit(
            &thread->state, &old_state, JOINED,
            memory_order_acq_rel, memory_order_acquire)) {
        do {
            switch (_mx_futex_wait(&thread->state, JOINED, MX_TIME_INFINITE)) {
            case MX_ERR_BAD_STATE:   // Never blocked because it had changed.
            case MX_OK:              // Woke up because it might have changed.
                old_state = atomic_load_explicit(&thread->state,
                                                 memory_order_acquire);
                break;
            default:
                __builtin_trap();
            }
        } while (old_state == JOINED);
        if (old_state != DONE)
            __builtin_trap();
    } else {
        switch (old_state) {
        case JOINED:
        case DETACHED:
            return MX_ERR_INVALID_ARGS;
        case DONE:
            break;
        default:
            __builtin_trap();
        }
    }

    // The thread has already closed its own handle.
    return MX_OK;
}

mx_status_t mxr_thread_detach(mxr_thread_t* thread) {
    int old_state = JOINABLE;
    if (!atomic_compare_exchange_strong_explicit(
            &thread->state, &old_state, DETACHED,
            memory_order_acq_rel, memory_order_relaxed)) {
        switch (old_state) {
        case DETACHED:
        case JOINED:
            return MX_ERR_INVALID_ARGS;
        case DONE:
            return MX_ERR_BAD_STATE;
        default:
            __builtin_trap();
        }
    }

    return MX_OK;
}

bool mxr_thread_detached(mxr_thread_t* thread) {
    int state = atomic_load_explicit(&thread->state, memory_order_acquire);
    return state == DETACHED;
}

mx_status_t mxr_thread_kill(mxr_thread_t* thread) {
    mx_status_t status = _mx_task_kill(thread->handle);
    if (status != MX_OK)
        return status;

    mx_handle_t handle = thread->handle;
    thread->handle = MX_HANDLE_INVALID;

    int old_state = atomic_exchange_explicit(&thread->state, DONE,
                                             memory_order_release);
    switch (old_state) {
    case DETACHED:
    case JOINABLE:
        return _mx_handle_close(handle);

    case JOINED:
        // We're now in a race with mxr_thread_join.  It might complete
        // and free the memory before we could fetch the handle from it.
        // So we use the copy we fetched before.  In case someone is
        // blocked in mxr_thread_join, wake the futex.  Doing so is a
        // benign race: if the address is unmapped and our futex_wake
        // fails, it's OK; if the memory is reused for something else
        // and our futex_wake tickles somebody completely unrelated,
        // well, that's why futex_wait can always have spurious wakeups.
        status = _mx_handle_close(handle);
        if (status != MX_OK)
            (void)_mx_futex_wake(&thread->state, 1);
        return status;
    }

    __builtin_trap();
}

mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread) {
    return thread->handle;
}

mx_status_t mxr_thread_adopt(mx_handle_t handle, mxr_thread_t* thread) {
    initialize_thread(thread, handle, false);
    return handle == MX_HANDLE_INVALID ? MX_ERR_BAD_HANDLE : MX_OK;
}
