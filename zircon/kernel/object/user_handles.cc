// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/user_handles.h"

#include <ktl/algorithm.h>

namespace {
// Basic checks for a |handle| to be able to be sent via |channel|.
static zx_status_t handle_checks_locked(const Handle* handle, const Dispatcher* channel,
                                        zx_handle_op_t operation, zx_rights_t desired_rights,
                                        zx_obj_type_t type) {
  if (!handle)
    return ZX_ERR_BAD_HANDLE;
  if (!handle->HasRights(ZX_RIGHT_TRANSFER))
    return ZX_ERR_ACCESS_DENIED;
  if (handle->dispatcher().get() == channel)
    return ZX_ERR_NOT_SUPPORTED;
  if (type != ZX_OBJ_TYPE_NONE && handle->dispatcher()->get_type() != type)
    return ZX_ERR_WRONG_TYPE;
  if (operation != ZX_HANDLE_OP_MOVE && operation != ZX_HANDLE_OP_DUPLICATE)
    return ZX_ERR_INVALID_ARGS;
  if (desired_rights != ZX_RIGHT_SAME_RIGHTS) {
    if ((handle->rights() & desired_rights) != desired_rights) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  if ((operation == ZX_HANDLE_OP_DUPLICATE) && !handle->HasRights(ZX_RIGHT_DUPLICATE))
    return ZX_ERR_ACCESS_DENIED;
  return ZX_OK;
}

}  // namespace

zx_status_t get_user_handles_to_consume(user_in_ptr<const zx_handle_t> user_handles, size_t offset,
                                        size_t chunk_size, zx_handle_t* handles) {
  return user_handles.copy_array_from_user(handles, chunk_size, offset);
}

zx_status_t get_user_handles_to_consume(user_inout_ptr<zx_handle_disposition_t> user_handles,
                                        size_t offset, size_t chunk_size, zx_handle_t* handles) {
  zx_handle_disposition_t local_handle_disposition[kMaxMessageHandles] = {};

  chunk_size = ktl::min<size_t>(chunk_size, kMaxMessageHandles);

  zx_status_t status =
      user_handles.copy_array_from_user(local_handle_disposition, chunk_size, offset);
  if (status != ZX_OK) {
    return status;
  }

  for (size_t i = 0; i < chunk_size; i++) {
    // !ZX_HANDLE_OP_DUPLICATE is used to capture the case where we failed
    // due to a bad operational arg.
    if (local_handle_disposition[i].operation != ZX_HANDLE_OP_DUPLICATE) {
      handles[i] = local_handle_disposition[i].handle;
    }
  }
  return ZX_OK;
}

// This overload is used by zx_channel_write.
zx::status<Handle*> get_handle_for_message_locked(ProcessDispatcher* process,
                                                  const Dispatcher* channel,
                                                  const zx_handle_t* handle_val) {
  Handle* source = process->GetHandleLocked(*handle_val);

  auto status = handle_checks_locked(source, channel, ZX_HANDLE_OP_MOVE, ZX_RIGHT_SAME_RIGHTS,
                                     ZX_OBJ_TYPE_NONE);
  if (status != ZX_OK)
    return zx::error(status);

  return zx::ok(process->RemoveHandleLocked(source).release());
}

// This overload is used by zx_channel_write_etc.
zx::status<Handle*> get_handle_for_message_locked(ProcessDispatcher* process,
                                                  const Dispatcher* channel,
                                                  zx_handle_disposition_t* handle_disposition) {
  Handle* source = process->GetHandleLocked(handle_disposition->handle);

  const zx_handle_op_t operation = handle_disposition->operation;
  const zx_rights_t desired_rights = handle_disposition->rights;
  const zx_obj_type_t type = handle_disposition->type;

  auto status = handle_checks_locked(source, channel, operation, desired_rights, type);
  if (status != ZX_OK) {
    handle_disposition->result = status;
    return zx::error(status);
  }
  // This if() block is purely an optimization and can be removed without
  // the rest of the function having to change.
  if ((operation == ZX_HANDLE_OP_MOVE) && (desired_rights == ZX_RIGHT_SAME_RIGHTS)) {
    return zx::ok(process->RemoveHandleLocked(source).release());
  }
  // For the non-optimized case, we always need to create a new handle because
  // the rights are a const member of Handle.
  const auto dest_rights =
      (desired_rights == ZX_RIGHT_SAME_RIGHTS) ? source->rights() : desired_rights;

  auto raw_handle = Handle::Dup(source, dest_rights).release();
  if (!raw_handle) {
    // It's possible for the dup operation to fail if we run out of handles exactly
    // at this point.
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // Use !ZX_HANDLE_OP_DUPLICATE so that we handle the case where operation
  // is an invalid value.
  if (operation != ZX_HANDLE_OP_DUPLICATE) {
    process->RemoveHandleLocked(source);
  }
  return zx::ok(raw_handle);
}
