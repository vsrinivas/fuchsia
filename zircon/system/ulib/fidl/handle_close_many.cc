// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

#ifdef __Fuchsia__
#ifdef __cplusplus

#include <zircon/syscalls.h>

zx_status_t FidlHandleCloseMany(const zx_handle_t* handles, size_t num_handles) {
  return zx_handle_close_many(handles, num_handles);
}

zx_status_t FidlHandleCloseMany(const zx_handle_disposition_t* handle_dispositions,
                                size_t num_handles) {
  ZX_ASSERT(num_handles <= ZX_CHANNEL_MAX_MSG_HANDLES);
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (size_t i = 0; i < num_handles; i++) {
    handles[i] = handle_dispositions[i].handle;
  }
  return zx_handle_close_many(handles, num_handles);
}

zx_status_t FidlHandleCloseMany(const zx_handle_info_t* handle_infos, size_t num_handles) {
  ZX_ASSERT(num_handles <= ZX_CHANNEL_MAX_MSG_HANDLES);
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (size_t i = 0; i < num_handles; i++) {
    handles[i] = handle_infos[i].handle;
  }
  return zx_handle_close_many(handles, num_handles);
}

#endif  // __cplusplus
#endif  // __Fuchsia__
