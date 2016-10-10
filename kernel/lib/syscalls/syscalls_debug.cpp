// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_region.h>

#include <lib/console.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/ktrace.h>

#include <lk/init.h>
#include <platform/debug.h>

#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_copy.h>

#include <mxtl/array.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxDebugWriteSize = 256u;
constexpr mx_size_t kMaxDebugReadBlock = 64 * 1024u * 1024u;
constexpr mx_size_t kMaxDebugWriteBlock = 64 * 1024u * 1024u;

constexpr uint32_t kMaxThreadStateSize = MX_MAX_THREAD_STATE_SIZE;

#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif

int sys_debug_read(mx_handle_t handle, void* ptr, uint32_t len) {
    LTRACEF("ptr %p\n", ptr);

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    if (!len)
        return 0;
    // TODO: remove this cast.
    auto uptr = reinterpret_cast<uint8_t*>(ptr);
    auto end = uptr + len;

    for (; uptr != end; ++uptr) {
        int c = getchar();
        if (c < 0)
            break;

        if (c == '\r')
            c = '\n';
        if (copy_to_user_u8_unsafe(uptr, static_cast<uint8_t>(c)) != NO_ERROR)
            break;
    }
    // TODO: fix this cast, which can overflow.
    return static_cast<int>(reinterpret_cast<char*>(uptr) - reinterpret_cast<char*>(ptr));
}

int sys_debug_write(const void* ptr, uint32_t len) {
    LTRACEF("ptr %p, len %u\n", ptr, len);

    if (len > kMaxDebugWriteSize)
        len = kMaxDebugWriteSize;

    char buf[kMaxDebugWriteSize];
    if (magenta_copy_from_user(ptr, buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    for (uint32_t i = 0; i < len; i++) {
        platform_dputc(buf[i]);
    }
    return len;
}

int sys_debug_send_command(mx_handle_t handle, const void* ptr, uint32_t len) {
    LTRACEF("ptr %p, len %u\n", ptr, len);

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    if (len > kMaxDebugWriteSize)
        return ERR_INVALID_ARGS;

    char buf[kMaxDebugWriteSize + 2];
    if (magenta_copy_from_user(ptr, buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    buf[len] = '\n';
    buf[len + 1] = 0;
    return console_run_script(buf);
}

// Given a task (job or process), obtain a handle to that task's child
// which has the matching koid.  For now, a handle of MX_HANDLE_INVALID
// is the "root" handle that is the parent of processes.  This will
// change to a real root resource handle so only privileged callers may
// obtain top level processes (and eventually jobs) directly from a koid
mx_handle_t sys_debug_task_get_child(mx_handle_t handle, uint64_t koid) {
    // TODO(dje): What rights to use here could depend on whether the task
    // is a process, thread, etc. Plus if |handle| has extra rights, say
    // some kind of "debug" rights, then we may want to confer those rights
    // onto the returned handle. Plus as new rights get added to processes,
    // threads, etc. we don't want to have to continually update this value.
    const auto kDebugRights =
        MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
        MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY;

    auto up = ProcessDispatcher::GetCurrent();

    if (handle == MX_HANDLE_INVALID) {
        //TODO: lookup process from job
        //TODO: lookup job from global resource handle
        // for now handle == 0 means look up process globally
        auto process = ProcessDispatcher::LookupProcessById(koid);
        if (!process)
            return ERR_NOT_FOUND;

        HandleUniquePtr process_h(
            MakeHandle(mxtl::RefPtr<Dispatcher>(process.get()), kDebugRights));
        if (!process_h)
            return ERR_NO_MEMORY;

        auto process_hv = up->MapHandleToValue(process_h.get());
        up->AddHandle(mxtl::move(process_h));
        return process_hv;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;
    if (!up->GetDispatcher(handle, &dispatcher, &rights))
        return ERR_BAD_HANDLE;

    auto process = dispatcher->get_specific<ProcessDispatcher>();
    if (process) {
        auto thread = process->LookupThreadById(koid);
        if (!thread)
            return ERR_NOT_FOUND;
        auto td = mxtl::RefPtr<Dispatcher>(thread->dispatcher());
        if (!td)
            return ERR_NOT_FOUND;
        HandleUniquePtr thread_h(MakeHandle(td, kDebugRights));
        if (!thread_h)
            return ERR_NO_MEMORY;

        auto thread_hv = up->MapHandleToValue(thread_h.get());
        up->AddHandle(mxtl::move(thread_h));
        return thread_hv;
    }

    return ERR_WRONG_TYPE;
}

mx_handle_t sys_debug_transfer_handle(mx_handle_t proc, mx_handle_t src_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcher(proc, &process,
                                           MX_RIGHT_READ | MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // Disallow this call on self.
    if (process.get() == up)
        return ERR_INVALID_ARGS;

    HandleUniquePtr handle = up->RemoveHandle(src_handle);
    if (!handle)
        return ERR_BAD_HANDLE;

    auto dest_hv = process->MapHandleToValue(handle.get());
    process->AddHandle(mxtl::move(handle));
    return dest_hv;
}

mx_ssize_t sys_debug_read_memory(mx_handle_t proc, uintptr_t vaddr, mx_size_t len, user_ptr<void> buffer) {
    if (!buffer)
        return ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugReadBlock)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcher(proc, &process,
                                           MX_RIGHT_READ | MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // Disallow this call on self.
    if (process.get() == up)
        return ERR_INVALID_ARGS;

    auto aspace = process->aspace();
    if (!aspace)
        return ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return ERR_NO_MEMORY;

    auto vmo = region->vmo();
    if (!vmo)
        return ERR_NO_MEMORY;

    uint64_t offset = vaddr - region->base() + region->object_offset();
    size_t read = 0;

    status_t st = vmo->ReadUser(buffer, offset, len, &read);

    if (st < 0)
        return st;

    return static_cast<mx_ssize_t>(read);
}

mx_ssize_t sys_debug_write_memory(mx_handle_t proc, uintptr_t vaddr, mx_size_t len, user_ptr<const void> buffer) {
    if (!buffer)
        return ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugWriteBlock)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcher(proc, &process, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // Disallow this call on self.
    if (process.get() == up)
        return ERR_INVALID_ARGS;

    auto aspace = process->aspace();
    if (!aspace)
        return ERR_BAD_STATE;

    auto region = aspace->FindRegion(vaddr);
    if (!region)
        return ERR_NO_MEMORY;

    auto vmo = region->vmo();
    if (!vmo)
        return ERR_NO_MEMORY;

    uint64_t offset = vaddr - region->base() + region->object_offset();
    size_t written = 0;

    status_t st = vmo->WriteUser(buffer, offset, len, &written);

    if (st < 0)
        return st;

    return static_cast<mx_ssize_t>(written);
}

mx_ssize_t sys_ktrace_read(mx_handle_t handle, void* ptr, uint32_t off, uint32_t len) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    return ktrace_read_user(ptr, off, len);
}

mx_status_t sys_ktrace_control(mx_handle_t handle, uint32_t action, uint32_t options, user_ptr<void> ptr) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    switch (action) {
    case KTRACE_ACTION_NEW_PROBE: {
        char name[MX_MAX_NAME_LEN];
        if (ptr.copy_array_from_user(name, sizeof(name - 1)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        name[sizeof(name) - 1] = 0;
        return ktrace_control(action, options, name);
    }
    default:
        return ktrace_control(action, options, nullptr);
    }
}

void sys_ktrace_write(mx_handle_t handle, uint32_t event_id, uint32_t arg0, uint32_t arg1) {
    // TODO: finer grained validation
    if (validate_resource_handle(handle) < 0) {
        return;
    }
    if (event_id > 0x7FF) {
        return;
    }
    uint32_t* args = (uint32_t*) ktrace_open(TAG_PROBE_24(event_id));
    if (args) {
        args[0] = arg0;
        args[1] = arg1;
    }
}

mx_status_t sys_thread_read_state(mx_handle_t handle, uint32_t state_kind,
                                  user_ptr<void> _buffer_ptr, user_ptr<uint32_t> _buffer_len)
{
    LTRACEF("handle %d, state_kind %u\n", handle, state_kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(dje): debug rights
    mxtl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcher(handle, &thread, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    uint32_t buffer_len;
    if (_buffer_len.copy_from_user(&buffer_len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // avoid malloc'ing insane amounts
    if (buffer_len > kMaxThreadStateSize)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    uint8_t* tmp_buf = new (&ac) uint8_t [buffer_len];
    if (!ac.check())
        return ERR_NO_MEMORY;
    mxtl::Array<uint8_t> bytes(tmp_buf, buffer_len);

    status = thread->thread()->ReadState(state_kind, bytes.get(), &buffer_len);

    // Always set the actual size so the caller can provide larger buffers.
    // The value is only usable if the status is NO_ERROR or ERR_BUFFER_TOO_SMALL.
    if (status == NO_ERROR || status == ERR_BUFFER_TOO_SMALL) {
        if (_buffer_len.copy_to_user(buffer_len) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (status != NO_ERROR)
        return status;

    status = _buffer_ptr.copy_array_to_user(bytes.get(), buffer_len);
    if (status != NO_ERROR)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

mx_status_t sys_thread_write_state(mx_handle_t handle, uint32_t state_kind,
                                   user_ptr<const void> _buffer_ptr, uint32_t buffer_len)
{
    LTRACEF("handle %d, state_kind %u\n", handle, state_kind);

    auto up = ProcessDispatcher::GetCurrent();

    // TODO(dje): debug rights
    mxtl::RefPtr<ThreadDispatcher> thread;
    mx_status_t status = up->GetDispatcher(handle, &thread, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // avoid malloc'ing insane amounts
    if (buffer_len > kMaxThreadStateSize)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    uint8_t* tmp_buf = new (&ac) uint8_t [buffer_len];
    if (!ac.check())
        return ERR_NO_MEMORY;
    mxtl::Array<uint8_t> bytes(tmp_buf, buffer_len);

    status = _buffer_ptr.copy_array_from_user(bytes.get(), buffer_len);
    if (status != NO_ERROR)
        return ERR_INVALID_ARGS;

    // TODO(dje): Setting privileged values in registers.
    status = thread->thread()->WriteState(state_kind, bytes.get(), buffer_len, false);
    return status;
}
