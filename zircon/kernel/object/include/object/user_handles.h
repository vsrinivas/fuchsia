// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_USER_HANDLES_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_USER_HANDLES_H_

#include <lib/user_copy/user_ptr.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <ktl/algorithm.h>
#include <object/process_dispatcher.h>

// Extracts the handles that would be consumed on syscalls with handle_release semantics
// from |offset| to  |offset + chunk_size| from |user_handles| and returns them
// in |handles| which must be at of at least size |chunk_size|.
zx_status_t get_user_handles_to_consume(user_in_ptr<const zx_handle_t> user_handles, size_t offset,
                                        size_t chunk_size, zx_handle_t* handles);

zx_status_t get_user_handles_to_consume(user_inout_ptr<zx_handle_disposition_t> user_handles,
                                        size_t offset, size_t chunk_size, zx_handle_t* handles);

// Removes the handles pointed by |user_handles| from |process|. Returns ZX_OK if all handles
// have been removed, error otherwise. It only stops early if get_user_handles() fails.
template <typename T>
zx_status_t RemoveUserHandles(T user_handles, size_t num_handles, ProcessDispatcher* process) {
  zx_handle_t handles[kMaxMessageHandles];
  size_t offset = 0u;
  zx_status_t status = ZX_OK;

  while (offset < num_handles) {
    // We process |num_handles| in chunks of |kMaxMessageHandles| because we don't have
    // a limit on how large |num_handles| can be.
    auto chunk_size = ktl::min<size_t>(num_handles - offset, kMaxMessageHandles);
    status = get_user_handles_to_consume(user_handles, offset, chunk_size, handles);
    if (status != ZX_OK) {
      break;
    }

    status = process->RemoveHandles(ktl::span(handles, chunk_size));
    offset += chunk_size;
  }
  return status;
}

// Returns a |raw_handle| that should be sent over |channel|. In case
// of error, the return value should be reflected back to the user.
zx::status<Handle*> get_handle_for_message_locked(ProcessDispatcher* process,
                                                  const Dispatcher* channel,
                                                  const zx_handle_t* handle_val)
    TA_REQ(process->handle_table_lock());

zx::status<Handle*> get_handle_for_message_locked(ProcessDispatcher* process,
                                                  const Dispatcher* channel,
                                                  zx_handle_disposition_t* handle_disposition)
    TA_REQ(process->handle_table_lock());

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_USER_HANDLES_H_
