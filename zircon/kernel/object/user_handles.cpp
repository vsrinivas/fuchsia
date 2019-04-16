// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/user_handles.h>

zx_status_t get_user_handles(
    user_in_ptr<const zx_handle_t> user_handles, size_t offset, size_t chunk_size,
    zx_handle_t* handles) {
    return user_handles.copy_array_from_user(handles, chunk_size, offset);
}

zx_status_t get_user_handles_to_consume(
    user_in_ptr<const zx_handle_t> user_handles, size_t offset, size_t chunk_size,
    zx_handle_t* handles) {
    return get_user_handles(user_handles, offset, chunk_size, handles);
}
