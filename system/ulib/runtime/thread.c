// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <runtime/thread.h>

#include <limits.h>
#include <magenta/syscalls.h>
#include <magenta/tlsroot.h>
#include <runtime/mutex.h>
#include <runtime/process.h>
#include <runtime/tls.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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

struct mxr_thread {
    mx_handle_t handle;
    int return_value;
    mxr_thread_entry_t entry;
    void* arg;

    int errno_value;

    mxr_mutex_t state_lock;
    int state;

    mx_tls_root_t tls_root;
};

static mx_status_t allocate_thread_page(mxr_thread_t** thread_out) {
    // TODO(kulakowski) Pull out this allocation function out
    // somewhere once we have the ability to hint to the vm how and
    // where to allocate threads, stacks, heap etc.

    mx_size_t len = sizeof(mxr_thread_t);
    // mx_tls_root_t already accounts for 1 tls slot.
    len += (MXR_TLS_SLOT_MAX - 1) * sizeof(void*);
    len += PAGE_SIZE - 1;
    len &= ~(PAGE_SIZE - 1);

    mx_handle_t vmo = _magenta_vm_object_create(len);
    if (vmo < 0)
        return (mx_status_t)vmo;

    // TODO(kulakowski) Track process handle.
    mx_handle_t self_handle = 0;
    uintptr_t mapping = 0;
    uint32_t flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    mx_status_t status = _magenta_process_vm_map(self_handle, vmo, 0, len, &mapping, flags);
    if (status != NO_ERROR) {
        _magenta_handle_close(vmo);
        return status;
    }

    _magenta_handle_close(vmo);
    *thread_out = (mxr_thread_t*)mapping;
    return NO_ERROR;
}

static mx_status_t deallocate_thread_page(mxr_thread_t* thread) {
    // TODO(kulakowski) Track process handle.
    mx_handle_t self_handle = 0;
    uintptr_t mapping = (uintptr_t)thread;
    return _magenta_process_vm_unmap(self_handle, mapping, 0u);
}

static mx_status_t thread_cleanup(mxr_thread_t* thread, int* return_value_out) {
    mx_status_t status = _magenta_handle_close(thread->handle);
    if (status != NO_ERROR)
        return status;
    int return_value = thread->return_value;
    status = deallocate_thread_page(thread);
    if (status != NO_ERROR)
        return status;
    if (return_value_out)
        *return_value_out = return_value;
    return NO_ERROR;
}

static void init_tls(mxr_thread_t* thread) {
    thread->tls_root.self = &thread->tls_root;
    thread->tls_root.proc = mxr_process_get_info();
    thread->tls_root.proc = NULL;
    thread->tls_root.magic = MX_TLS_ROOT_MAGIC;
    thread->tls_root.flags = 0;
    thread->tls_root.maxslots = MXR_TLS_SLOT_MAX;
    memset(&thread->tls_root.slots, 0, MXR_TLS_SLOT_MAX * sizeof(void*));
    mxr_tls_root_set(&thread->tls_root);
    mxr_tls_set(MXR_TLS_SLOT_SELF, &thread->tls_root);
    mxr_tls_set(MXR_TLS_SLOT_ERRNO, &thread->errno_value);
}

static int thread_trampoline(void* ctx) {
    mxr_thread_t* thread = (mxr_thread_t*)ctx;

    init_tls(thread);

    thread->return_value = thread->entry(thread->arg);

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
            abort();
    }

    _magenta_thread_exit();
    return 0;
}

mx_status_t mxr_thread_create(mxr_thread_entry_t entry, void* arg, const char* name, mxr_thread_t** thread_out) {
    mxr_thread_t* thread = NULL;
    mx_status_t status = allocate_thread_page(&thread);
    if (status < 0)
        return status;

    thread->entry = entry;
    thread->arg = arg;
    thread->state_lock = MXR_MUTEX_INIT;
    thread->state = JOINABLE;

    if (name == NULL)
        name = "";
    size_t name_length = strlen(name) + 1;
    mx_handle_t handle = _magenta_thread_create(thread_trampoline, thread, name, name_length);
    if (handle < 0) {
        deallocate_thread_page(thread);
        return (mx_status_t)handle;
    }
    thread->handle = handle;
    *thread_out = thread;
    return NO_ERROR;
}

mx_status_t mxr_thread_join(mxr_thread_t* thread, int* return_value_out) {
    mxr_mutex_lock(&thread->state_lock);
    switch (thread->state) {
        case JOINED:
        case DETACHED:
            mxr_mutex_unlock(&thread->state_lock);
            return ERR_INVALID_ARGS;
        case JOINABLE: {
            thread->state = JOINED;
            mxr_mutex_unlock(&thread->state_lock);
            mx_status_t status = _magenta_handle_wait_one(thread->handle, MX_SIGNAL_SIGNALED,
                                                          MX_TIME_INFINITE, NULL, NULL);
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

void __mxr_thread_main(void) {
    mxr_tls_t self_slot = mxr_tls_allocate();
    mxr_tls_t errno_slot = mxr_tls_allocate();

    if (self_slot != MXR_TLS_SLOT_SELF ||
        errno_slot != MXR_TLS_SLOT_ERRNO)
        abort();

    mxr_thread_t* thread = NULL;
    allocate_thread_page(&thread);
    init_tls(thread);
    thread->state_lock = MXR_MUTEX_INIT;
    thread->state = JOINABLE;
    // TODO(kulakowski) Once the main thread is passed a handle, save it here.
    thread->handle = MX_HANDLE_INVALID;
}
