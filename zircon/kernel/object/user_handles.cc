// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/user_handles.h>

namespace {
// Basic checks for a |handle| to be able to be sent via |channel|.
static zx_status_t handle_checks_locked(const Handle* handle, const Dispatcher* channel) {
    if (!handle)
        return ZX_ERR_BAD_HANDLE;
    if (!handle->HasRights(ZX_RIGHT_TRANSFER))
        return ZX_ERR_ACCESS_DENIED;
    if (handle->dispatcher().get() == channel)
        return ZX_ERR_NOT_SUPPORTED;
    return ZX_OK;
}

}  //namespace

zx_status_t get_user_handles_to_consume(
    user_in_ptr<const zx_handle_t> user_handles, size_t offset, size_t chunk_size,
    zx_handle_t* handles) {
    return user_handles.copy_array_from_user(handles, chunk_size, offset);
}

zx_status_t get_user_handles_to_consume(
    user_inout_ptr<zx_handle_disposition_t> user_handles, size_t offset, size_t chunk_size,
    zx_handle_t* handles) {
    // TODO(cpu): implement for zx_channel_write_etc.
    return ZX_ERR_NOT_SUPPORTED;
}

// This overload is used by zx_channel_write.
zx_status_t get_handle_for_message_locked(
    ProcessDispatcher* process, const Dispatcher* channel,
    zx_handle_t handle_val, Handle** raw_handle) {

    Handle* source = process->GetHandleLocked(handle_val);

    auto status = handle_checks_locked(source, channel);
    if (status != ZX_OK)
        return status;

    *raw_handle = process->RemoveHandleLocked(source).release();
    return ZX_OK;
}

// This overload is used by zx_channel_write_etc.
zx_status_t get_handle_for_message_locked(
    ProcessDispatcher* process, const Dispatcher* channel,
    zx_handle_disposition_t handle_disposition, Handle** raw_handle) {
    //  TODO(cpu): implement for zx_channel_write_etc.
    return ZX_ERR_NOT_SUPPORTED;
}
