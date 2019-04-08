// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_ptr.h>
#include <lib/user_copy/user_ptr.h>
#include <object/process_dispatcher.h>

#include <zircon/types.h>

// Extracts the handles from |offset| to  |offset + chunk_size| from |user_handles| and
// returns them in |handles|.
zx_status_t get_user_handles(user_in_ptr<const zx_handle_t> user_handles, size_t offset,
                             size_t chunk_size, zx_handle_t* handles);

// Removes the handles pointed by |user_handles| from |process|. Returns ZX_OK if all handles
// have been removed, error otherwise. It only stops early if get_user_handles() fails.
template <typename T>
zx_status_t RemoveUserHandles(
    T user_handles, size_t num_handles, ProcessDispatcher* process) {
    zx_handle_t handles[kMaxMessageHandles];
    size_t offset = 0u;
    zx_status_t status = ZX_OK;

    while (offset < num_handles) {
        // We process |num_handles| in chunks of |kMaxMessageHandles| because we don't have
        // a limit on how large |num_handles| can be.
        auto chunk_size = fbl::min<size_t>(num_handles - offset, kMaxMessageHandles);
        status = get_user_handles(user_handles, offset, chunk_size, handles);
        if (status != ZX_OK) {
            break;
        }

        status = process->RemoveHandles(handles, chunk_size);
        offset += chunk_size;
    }
    return status;
}
