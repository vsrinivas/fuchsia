// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif  // __Fuchsia__

zx_status_t FidlHandleCloseMany(const zx_handle_t* handles, size_t num_handles) {
#ifdef __Fuchsia__
  return zx_handle_close_many(handles, num_handles);
#else
  return ZX_OK;
#endif  // __Fuchsia__
}

zx_status_t FidlHandleDispositionCloseMany(const zx_handle_disposition_t* handle_dispositions,
                                           size_t num_handles) {
#ifdef __Fuchsia__
  ZX_ASSERT(num_handles <= ZX_CHANNEL_MAX_MSG_HANDLES);
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (size_t i = 0; i < num_handles; i++) {
    handles[i] = handle_dispositions[i].handle;
  }
  return zx_handle_close_many(handles, num_handles);
#else
  return ZX_OK;
#endif  // __Fuchsia__
}

zx_status_t FidlHandleInfoCloseMany(const zx_handle_info_t* handle_infos, size_t num_handles) {
#ifdef __Fuchsia__
  ZX_ASSERT(num_handles <= ZX_CHANNEL_MAX_MSG_HANDLES);
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (size_t i = 0; i < num_handles; i++) {
    handles[i] = handle_infos[i].handle;
  }
  return zx_handle_close_many(handles, num_handles);
#else
  return ZX_OK;
#endif  // __Fuchsia__
}
