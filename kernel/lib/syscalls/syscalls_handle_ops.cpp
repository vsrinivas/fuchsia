// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_handle_close(mx_handle_t handle_value) {
    LTRACEF("handle %d\n", handle_value);
    auto up = ProcessDispatcher::GetCurrent();
    HandleUniquePtr handle(up->RemoveHandle(handle_value));
    if (!handle)
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);
    return NO_ERROR;
}

mx_handle_t sys_handle_duplicate(mx_handle_t handle_value, mx_rights_t rights) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t dup_hv;

    {
        AutoLock lock(up->handle_table_lock());
        Handle* source = up->GetHandle_NoLock(handle_value);
        if (!source)
            return up->BadHandle(handle_value, ERR_BAD_HANDLE);

        if (!magenta_rights_check(source->rights(), MX_RIGHT_DUPLICATE))
            return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

        HandleUniquePtr dest;
        if (rights == MX_RIGHT_SAME_RIGHTS) {
            dest.reset(DupHandle(source, source->rights()));
        } else {
            if ((source->rights() & rights) != rights)
                return ERR_INVALID_ARGS;
            dest.reset(DupHandle(source, rights));
        }
        if (!dest)
            return ERR_NO_MEMORY;

        dup_hv = up->MapHandleToValue(dest.get());
        up->AddHandle_NoLock(mxtl::move(dest));
    }

    return dup_hv;
}

mx_handle_t sys_handle_replace(mx_handle_t handle_value, mx_rights_t rights) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    HandleUniquePtr source;
    mx_handle_t replacement_hv;

    {
        AutoLock lock(up->handle_table_lock());
        source = up->RemoveHandle_NoLock(handle_value);
        if (!source)
            return up->BadHandle(handle_value, ERR_BAD_HANDLE);

        HandleUniquePtr dest;
        // Used only if |dest| doesn't (successfully) get set below.
        mx_status_t error = ERR_NO_MEMORY;
        if (rights == MX_RIGHT_SAME_RIGHTS) {
            dest.reset(DupHandle(source.get(), source->rights()));
        } else {
            if ((source->rights() & rights) != rights)
                error = ERR_INVALID_ARGS;
            else
                dest.reset(DupHandle(source.get(), rights));
        }

        if (!dest) {
            // Unwind: put |source| back!
            up->AddHandle_NoLock(mxtl::move(source));
            return error;
        }

        replacement_hv = up->MapHandleToValue(dest.get());
        up->AddHandle_NoLock(mxtl::move(dest));
    }

    return replacement_hv;
}
