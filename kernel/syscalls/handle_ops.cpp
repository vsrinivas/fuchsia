// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <fbl/auto_lock.h>

#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_handle_close(zx_handle_t handle_value) {
    LTRACEF("handle %x\n", handle_value);

    // Closing the "never a handle" invalid handle is not an error
    // It's like free(NULL).
    if (handle_value == ZX_HANDLE_INVALID)
        return ZX_OK;
    auto up = ProcessDispatcher::GetCurrent();
    HandleOwner handle(up->RemoveHandle(handle_value));
    if (!handle)
        return ZX_ERR_BAD_HANDLE;
    return ZX_OK;
}

static zx_status_t handle_dup_replace(
    bool is_replace, zx_handle_t handle_value, zx_rights_t rights,
    user_out_handle* out) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    {
        fbl::AutoLock lock(up->handle_table_lock());
        auto source = up->GetHandleLocked(handle_value);
        if (!source)
            return ZX_ERR_BAD_HANDLE;

        if (!is_replace) {
            if (!source->HasRights(ZX_RIGHT_DUPLICATE))
                return ZX_ERR_ACCESS_DENIED;
        }

        if (rights == ZX_RIGHT_SAME_RIGHTS) {
            rights = source->rights();
        } else if ((source->rights() & rights) != rights) {
            return ZX_ERR_INVALID_ARGS;
        }

        zx_status_t status = out->dup(source, rights);
        if (status != ZX_OK)
            return status;

        if (is_replace)
            up->RemoveHandleLocked(handle_value);
    }

    return ZX_OK;
}

zx_status_t sys_handle_duplicate(
    zx_handle_t handle_value, zx_rights_t rights, user_out_handle* out) {
    return handle_dup_replace(false, handle_value, rights, out);
}

zx_status_t sys_handle_replace(
    zx_handle_t handle_value, zx_rights_t rights, user_out_handle* out) {
    return handle_dup_replace(true, handle_value, rights, out);
}
