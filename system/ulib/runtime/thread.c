// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/thread.h>

#include <limits.h>
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

#define CHECK_THREAD(thread) do { \
    if (!thread || thread->magic != MXR_THREAD_MAGIC) \
        __builtin_trap(); \
    } while (0);

struct mxr_thread {
    mx_handle_t handle;
    intptr_t return_value;
    mxr_thread_entry_t entry;
    void* arg;

    uint64_t magic;

    uintptr_t stack;
    size_t stack_size;

    mxr_mutex_t state_lock;
    int state;
};

static mx_status_t allocate_thread_page(mxr_thread_t** thread_out) {
    // TODO(kulakowski) Pull out this allocation function out
    // somewhere once we have the ability to hint to the vm how and
    // where to allocate threads, stacks, heap etc.

    const mx_size_t len = sizeof(mxr_thread_t);

    mx_handle_t vmo = mx_vm_object_create(len);
    if (vmo < 0)
        return (mx_status_t)vmo;

    // TODO(kulakowski) Track process handle.
    mx_handle_t self_handle = 0;
    uintptr_t mapping = 0;
    uint32_t flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    mx_status_t status = mx_process_vm_map(self_handle, vmo, 0, len, &mapping, flags);
    if (status != NO_ERROR) {
        mx_handle_close(vmo);
        return status;
    }

    mx_handle_close(vmo);
    *thread_out = (mxr_thread_t*)mapping;
    return NO_ERROR;
}

static mx_status_t deallocate_thread_page(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    // TODO(kulakowski) Track process handle.
    mx_handle_t self_handle = 0;
    uintptr_t mapping = (uintptr_t)thread;
    return mx_process_vm_unmap(self_handle, mapping, 0u);
}

static mx_status_t thread_cleanup(mxr_thread_t* thread, intptr_t* return_value_out) {
    CHECK_THREAD(thread);
    mx_status_t status = mx_handle_close(thread->handle);
    thread->handle = 0;
    if (status != NO_ERROR)
        return status;
    intptr_t return_value = thread->return_value;
    status = deallocate_thread_page(thread);
    if (status != NO_ERROR)
        return status;
    if (return_value_out)
        *return_value_out = return_value;
    return NO_ERROR;
}

static void thread_trampoline(uintptr_t ctx) {
    mxr_thread_t* thread = (mxr_thread_t*)ctx;
    CHECK_THREAD(thread);
    mxr_thread_exit(thread, thread->entry(thread->arg));
}

_Noreturn void mxr_thread_exit(mxr_thread_t* thread, intptr_t return_value) {
    CHECK_THREAD(thread);
    thread->return_value = return_value;

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
        thread_cleanup(thread, NULL);
        break;
    case DONE:
        // Not reached.
        __builtin_trap();
    }

    mx_thread_exit();
}

// Local implementation so libruntime does not depend on libc.
static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (*s++ != '\0')
        ++len;
    return len;
}

// copied from launchpad/stack.h
// XXX(travisg) put in shared place?
//
// Given the (page-aligned) base and size of the stack mapping,
// compute the appropriate initial SP value for an initial thread
// according to the C calling convention for the machine.
static inline uintptr_t sp_from_mapping(mx_vaddr_t base, size_t size) {
    // Assume stack grows down.
    mx_vaddr_t sp = base + size;
#ifdef __x86_64__
    // The x86-64 ABI requires %rsp % 16 = 8 on entry.  The zero word
    // at (%rsp) serves as the return address for the outermost frame.
    sp -= 8;
#elif defined(__arm__) || defined(__aarch64__)
    // The ARMv7 and ARMv8 ABIs both just require that SP be aligned.
#else
# error what machine?
#endif
    return sp;
}

mx_status_t mxr_thread_create(mxr_thread_entry_t entry, void* arg, const char* name, mxr_thread_t** thread_out) {
    mxr_thread_t* thread = NULL;
    mx_status_t status = allocate_thread_page(&thread);
    if (status < 0)
        return status;
    *thread_out = thread;

    thread->entry = entry;
    thread->arg = arg;
    thread->state_lock = MXR_MUTEX_INIT;
    thread->state = JOINABLE;
    thread->magic = MXR_THREAD_MAGIC;

    // TODO(kulakowski) Track process handle.
    mx_handle_t self_handle = 0;

    if (name == NULL)
        name = "";
    size_t name_length = local_strlen(name) + 1;
    mx_handle_t handle = mx_thread_create(self_handle, name, name_length, 0);
    if (handle < 0) {
        deallocate_thread_page(thread);
        return (mx_status_t)handle;
    }

    // create a new stack for the new thread
    thread->stack_size = 1024*1024;
    mx_handle_t thread_stack_vmo = mx_vm_object_create(thread->stack_size);
    if (thread_stack_vmo < 0) {
        deallocate_thread_page(thread);
        mx_handle_close(handle);
        return thread_stack_vmo;
    }

    // map it
    status = mx_process_vm_map(self_handle, thread_stack_vmo, 0, thread->stack_size, &thread->stack,
            MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    mx_handle_close(thread_stack_vmo);
    if (status < 0) {
        deallocate_thread_page(thread);
        mx_handle_close(handle);
        return status;
    }

    // compute the starting address of the stack
    uintptr_t sp = sp_from_mapping(thread->stack, thread->stack_size);

    // kick off the new thread
    status = mx_thread_start(handle, (uintptr_t)thread_trampoline, sp, (uintptr_t)thread);
    if (status < 0) {
        deallocate_thread_page(thread);
        mx_handle_close(handle);
        return status;
    }

    thread->handle = handle;
    return NO_ERROR;
}

mx_status_t mxr_thread_join(mxr_thread_t* thread, intptr_t* return_value_out) {
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
        mx_status_t status = mx_handle_wait_one(thread->handle, MX_SIGNAL_SIGNALED,
                                                      MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR)
            return status;
        break;
    }
    case DONE:
        mxr_mutex_unlock(&thread->state_lock);
        break;
    }

    return thread_cleanup(thread, return_value_out);
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
        status = thread_cleanup(thread, NULL);
        break;
    }

    return status;
}

mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread) {
    CHECK_THREAD(thread);
    return thread->handle;
}

mxr_thread_t* __mxr_thread_main(void) {
    mxr_thread_t* thread = NULL;
    allocate_thread_page(&thread);
    thread->state_lock = MXR_MUTEX_INIT;
    thread->state = JOINABLE;
    thread->magic = MXR_THREAD_MAGIC;
    // TODO(kulakowski) Once the main thread is passed a handle, save it here.
    thread->handle = MX_HANDLE_INVALID;
    return thread;
}
