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

#include <mxtl/user_ptr.h>

#include <lib/console.h>
#include <lib/user_copy.h>

#include <lk/init.h>
#include <platform/debug.h>

#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxDebugWriteSize = 256u;
constexpr mx_size_t kMaxDebugReadBlock = 64 * 1024u * 1024u;

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
    LTRACEF("ptr %p, len %d\n", ptr, len);

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
    LTRACEF("ptr %p, len %d\n", ptr, len);

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

mx_handle_t sys_debug_get_process(mx_handle_t handle, uint64_t koid) {
    const auto kProcessDebugRights =
        MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;
    // TODO(cpu): wire |handle| to be a resource handle. For now
    // check that is zero.
    if (handle)
        return ERR_INVALID_ARGS;

    auto process = ProcessDispatcher::LookupProcessById(koid);
    if (!process)
        return ERR_NOT_FOUND;

    HandleUniquePtr process_h(
        MakeHandle(mxtl::RefPtr<Dispatcher>(process.get()), kProcessDebugRights));
    if (!process_h)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    auto process_hv = up->MapHandleToValue(process_h.get());
    up->AddHandle(mxtl::move(process_h));
    return process_hv;
}

mx_handle_t sys_debug_transfer_handle(mx_handle_t proc, mx_handle_t src_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<Dispatcher> pd;
    uint32_t rights;
    if (!up->GetDispatcher(proc, &pd, &rights))
        return ERR_BAD_HANDLE;

    auto process = pd->get_process_dispatcher();
    if (!process)
        return ERR_WRONG_TYPE;

    // Disallow this call on self.
    if (process == up)
        return ERR_INVALID_ARGS;

    if (!magenta_rights_check(rights, MX_RIGHT_READ | MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

    HandleUniquePtr handle = up->RemoveHandle(src_handle);
    if (!handle)
        return ERR_BAD_HANDLE;

    auto dest_hv = process->MapHandleToValue(handle.get());
    process->AddHandle(mxtl::move(handle));
    return dest_hv;
}

mx_ssize_t sys_debug_read_memory(mx_handle_t proc, uintptr_t vaddr, mx_size_t len, void* buffer) {
    if (!buffer)
        return ERR_INVALID_ARGS;
    if (len == 0 || len > kMaxDebugReadBlock)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<Dispatcher> pd;
    uint32_t rights;
    if (!up->GetDispatcher(proc, &pd, &rights))
        return ERR_BAD_HANDLE;

    auto process = pd->get_process_dispatcher();
    if (!process)
        return ERR_WRONG_TYPE;

    // Disallow this call on self.
    if (process == up)
        return ERR_INVALID_ARGS;

    if (!magenta_rights_check(rights, MX_RIGHT_READ | MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;

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
