// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/thread.h>

#include <limits.h>
#include <magenta/stack.h>
#include <magenta/syscalls.h>
#include <runtime/mutex.h>
#include <runtime/tls.h>
#include <stddef.h>

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

#define MXR_THREAD_MAGIC 0x97c40acdb29ee45dULL

#define CHECK_THREAD(thread)                              \
    do {                                                  \
        if (!thread || thread->magic != MXR_THREAD_MAGIC) \
            __builtin_trap();                             \
    } while (0)

struct mxr_thread {
    mx_handle_t handle;
    mxr_thread_entry_t entry;
    void* arg;

    uint64_t magic;

    mxr_mutex_t state_lock;
    int state;
};

static mx_status_t allocate_thread_page(mxr_thread_t** thread_out) {
    // TODO(kulakowski) Pull out this allocation function out
    // somewhere once we have the ability to hint to the vm how and
    // where to allocate threads, stacks, heap etc.

    const mx_size_t len = sizeof(mxr_thread_t);

    mx_handle_t vmo = _mx_vmo_create(len);
    if (vmo < 0)
        return (mx_status_t)vmo;

    uintptr_t mapping = 0;
    uint32_t flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    mx_status_t status = _mx_process_map_vm(mx_process_self(), vmo, 0, len,
                                            &mapping, flags);
    _mx_handle_close(vmo);
    if (status != NO_ERROR)
        return status;
    mxr_thread_t* thread = (mxr_thread_t*)mapping;
    thread->state_lock = MXR_MUTEX_INIT;
    thread->state = JOINABLE;
    thread->magic = MXR_THREAD_MAGIC;
    *thread_out = thread;
    return NO_ERROR;
}

static mx_status_t deallocate_thread_page(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    uintptr_t mapping = (uintptr_t)thread;
    return _mx_process_unmap_vm(mx_process_self(), mapping, 0u);
}

static mx_status_t thread_cleanup(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    mx_status_t status = _mx_handle_close(thread->handle);
    thread->handle = 0;
    if (status != NO_ERROR)
        return status;
    return deallocate_thread_page(thread);
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
        thread_cleanup(thread);
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

mx_status_t mxr_thread_create(const char* name, mxr_thread_t** thread_out) {
    mxr_thread_t* thread = NULL;
    mx_status_t status = allocate_thread_page(&thread);
    if (status < 0)
        return status;

    if (name == NULL)
        name = "";
    size_t name_length = local_strlen(name) + 1;
    mx_handle_t handle = _mx_thread_create(mx_process_self(), name, name_length, 0);
    if (handle < 0) {
        deallocate_thread_page(thread);
        return (mx_status_t)handle;
    }

    thread->handle = handle;

    *thread_out = thread;
    return NO_ERROR;
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
    if (status < 0) {
        mx_handle_t handle = thread->handle;
        deallocate_thread_page(thread);
        _mx_handle_close(handle);
        return status;
    }

    return NO_ERROR;
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
            thread->handle, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR)
            return status;
        break;
    }
    case DONE:
        mxr_mutex_unlock(&thread->state_lock);
        break;
    }

    return thread_cleanup(thread);
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
        status = thread_cleanup(thread);
        break;
    }

    return status;
}

void mxr_thread_destroy(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    _mx_handle_close(thread->handle);
    deallocate_thread_page(thread);
}

mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    return thread->handle;
}

mxr_thread_t* __mxr_thread_main(void) {
    mxr_thread_t* thread = NULL;
    allocate_thread_page(&thread);
    // TODO(kulakowski) Once the main thread is passed a handle, save it here.
    thread->handle = MX_HANDLE_INVALID;
    return thread;
}
