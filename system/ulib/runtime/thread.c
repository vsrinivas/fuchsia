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
// - If it returns before one of those calls is made, it transitions to DONE.
// No other transitions occur.
enum {
    JOINABLE,
    JOINED,
    DETACHED,
    DONE,
};

#define MXR_THREAD_MAGIC_VALID          UINT64_C(0x97c40acdb29ee45d)
#define MXR_THREAD_MAGIC_DESTROYED      UINT64_C(0x97c0acdb29ee445d)
#define MXR_THREAD_MAGIC_STILLBORN      UINT64_C(0xc70acdb29e9e445d)

#define CHECK_THREAD(thread)                                           \
    do {                                                               \
        if (thread == NULL || thread->magic != MXR_THREAD_MAGIC_VALID) \
            __builtin_trap();                                          \
    } while (0)

mx_status_t mxr_thread_destroy(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    mx_handle_t handle = thread->handle;
    thread->handle = MX_HANDLE_INVALID;
    thread->magic = MXR_THREAD_MAGIC_DESTROYED;
    return handle == MX_HANDLE_INVALID ? NO_ERROR : _mx_handle_close(handle);
}

static void thread_trampoline(uintptr_t ctx) {
    mxr_thread_t* thread = (mxr_thread_t*)ctx;
    CHECK_THREAD(thread);
    thread->entry(thread->arg);
    mxr_thread_exit(thread);
}

_Noreturn void mxr_thread_exit(mxr_thread_t* thread) {
    CHECK_THREAD(thread);

    mxr_mutex_lock(&thread->state_lock);
    switch (thread->state) {
    case JOINED:
        mxr_mutex_unlock(&thread->state_lock);
        break;
    case JOINABLE:
        thread->state = DONE;
        mxr_mutex_unlock(&thread->state_lock);
        break;
    case DETACHED:
        mxr_mutex_unlock(&thread->state_lock);
        mxr_thread_destroy(thread);
        break;
    case DONE:
        // Not reached.
        __builtin_trap();
    }

    _mx_thread_exit();
}

// Local implementation so libruntime does not depend on libc.
static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (*s++ != '\0')
        ++len;
    return len;
}

static void initialize_thread(mxr_thread_t* thread, mx_handle_t handle) {
    *thread = (mxr_thread_t){
        .handle = handle,
        .state_lock = MXR_MUTEX_INIT,
        .state = JOINABLE,
        .magic = (handle == MX_HANDLE_INVALID ?
                  MXR_THREAD_MAGIC_STILLBORN :
                  MXR_THREAD_MAGIC_VALID),
    };
}

mx_status_t mxr_thread_create(mx_handle_t process, const char* name,
                              mxr_thread_t* thread) {
    initialize_thread(thread, MX_HANDLE_INVALID);
    if (name == NULL)
        name = "";
    size_t name_length = local_strlen(name) + 1;
    mx_status_t status = _mx_thread_create(process, name, name_length, 0,
                                           &thread->handle);
    if (status == NO_ERROR)
        thread->magic = MXR_THREAD_MAGIC_VALID;
    return status;
}

mx_status_t mxr_thread_start(mxr_thread_t* thread, uintptr_t stack_addr, size_t stack_size, mxr_thread_entry_t entry, void* arg) {
    CHECK_THREAD(thread);

    thread->entry = entry;
    thread->arg = arg;

    // compute the starting address of the stack
    uintptr_t sp = compute_initial_stack_pointer(stack_addr, stack_size);

    // kick off the new thread
    mx_status_t status = _mx_thread_start(thread->handle,
                                          (uintptr_t)thread_trampoline, sp,
                                          (uintptr_t)thread, 0);

    if (status != NO_ERROR)
        mxr_thread_destroy(thread);
    return status;
}

mx_status_t mxr_thread_join(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    mxr_mutex_lock(&thread->state_lock);
    switch (thread->state) {
    case JOINED:
    case DETACHED:
        mxr_mutex_unlock(&thread->state_lock);
        return ERR_INVALID_ARGS;
    case JOINABLE: {
        thread->state = JOINED;
        mxr_mutex_unlock(&thread->state_lock);
        mx_status_t status = _mx_handle_wait_one(
            thread->handle, MX_THREAD_SIGNALED, MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR)
            return status;
        break;
    }
    case DONE:
        mxr_mutex_unlock(&thread->state_lock);
        break;
    }

    return mxr_thread_destroy(thread);
}

mx_status_t mxr_thread_detach(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    mx_status_t status = NO_ERROR;
    mxr_mutex_lock(&thread->state_lock);
    switch (thread->state) {
    case JOINABLE:
        thread->state = DETACHED;
        mxr_mutex_unlock(&thread->state_lock);
        break;
    case JOINED:
    case DETACHED:
        mxr_mutex_unlock(&thread->state_lock);
        status = ERR_INVALID_ARGS;
        break;
    case DONE:
        mxr_mutex_unlock(&thread->state_lock);
        status = mxr_thread_destroy(thread);
        break;
    }

    return status;
}

mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    return thread->handle;
}

mx_status_t mxr_thread_adopt(mx_handle_t handle, mxr_thread_t* thread) {
    initialize_thread(thread, handle);
    return handle == MX_HANDLE_INVALID ? ERR_BAD_HANDLE : NO_ERROR;
}
