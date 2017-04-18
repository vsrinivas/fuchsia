// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_handle_close(mx_handle_t handle_value) {
    LTRACEF("handle %d\n", handle_value);
    auto up = ProcessDispatcher::GetCurrent();
    HandleOwner handle(up->RemoveHandle(handle_value));
    if (!handle)
        return ERR_BAD_HANDLE;
    return NO_ERROR;
}

mx_status_t sys_handle_duplicate(mx_handle_t handle_value, mx_rights_t rights, user_ptr<mx_handle_t> _out) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    {
        AutoLock lock(up->handle_table_lock());
        Handle* source = up->GetHandleLocked(handle_value);
        if (!source)
            return ERR_BAD_HANDLE;

        if (!magenta_rights_check(source, MX_RIGHT_DUPLICATE))
            return ERR_ACCESS_DENIED;

        HandleOwner dest;
        if (rights == MX_RIGHT_SAME_RIGHTS) {
            dest.reset(DupHandle(source, source->rights()));
        } else {
            if ((source->rights() & rights) != rights)
                return ERR_INVALID_ARGS;
            dest.reset(DupHandle(source, rights));
        }
        if (!dest)
            return ERR_NO_MEMORY;

        if (_out.copy_to_user(up->MapHandleToValue(dest)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandleLocked(mxtl::move(dest));
    }

    return NO_ERROR;
}

mx_status_t sys_handle_replace(mx_handle_t handle_value, mx_rights_t rights, user_ptr<mx_handle_t> _out) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    HandleOwner source;

    {
        AutoLock lock(up->handle_table_lock());
        source = up->RemoveHandleLocked(handle_value);
        if (!source)
            return ERR_BAD_HANDLE;

        HandleOwner dest;
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
            up->AddHandleLocked(mxtl::move(source));
            return error;
        }

        if (_out.copy_to_user(up->MapHandleToValue(dest)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandleLocked(mxtl::move(dest));
    }

    return NO_ERROR;
}
