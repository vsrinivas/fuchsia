// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/process_dispatcher.h>
#include <fbl/auto_lock.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_handle_close(mx_handle_t handle_value) {
    LTRACEF("handle %x\n", handle_value);

    // Closing the "never a handle" invalid handle is not an error
    // It's like free(NULL).
    if (handle_value == MX_HANDLE_INVALID)
        return MX_OK;
    auto up = ProcessDispatcher::GetCurrent();
    HandleOwner handle(up->RemoveHandle(handle_value));
    if (!handle)
        return MX_ERR_BAD_HANDLE;
    return MX_OK;
}

static mx_status_t handle_dup_replace(
    bool is_replace, mx_handle_t handle_value, mx_rights_t rights, user_ptr<mx_handle_t> _out) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    {
        fbl::AutoLock lock(up->handle_table_lock());
        auto source = up->GetHandleLocked(handle_value);
        if (!source)
            return MX_ERR_BAD_HANDLE;

        if (!is_replace) {
            if (!source->HasRights(MX_RIGHT_DUPLICATE))
                return MX_ERR_ACCESS_DENIED;
        }

        if (rights == MX_RIGHT_SAME_RIGHTS) {
            rights = source->rights();
        } else if ((source->rights() & rights) != rights) {
            return MX_ERR_INVALID_ARGS;
        }

        HandleOwner dest(DupHandle(source, rights, is_replace));
        if (!dest)
            return MX_ERR_NO_MEMORY;

        if (_out.copy_to_user(up->MapHandleToValue(dest)) != MX_OK)
            return MX_ERR_INVALID_ARGS;

        if (is_replace)
            up->RemoveHandleLocked(handle_value);

        up->AddHandleLocked(fbl::move(dest));
    }

    return MX_OK;
}

mx_status_t sys_handle_duplicate(
    mx_handle_t handle_value, mx_rights_t rights, user_ptr<mx_handle_t> _out) {
    return handle_dup_replace(false, handle_value, rights, _out);
}

mx_status_t sys_handle_replace(
    mx_handle_t handle_value, mx_rights_t rights, user_ptr<mx_handle_t> _out) {
    return handle_dup_replace(true, handle_value, rights, _out);
}
