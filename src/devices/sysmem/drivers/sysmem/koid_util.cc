// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koid_util.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

namespace sysmem_driver {

zx_status_t get_channel_koids(const zx::channel& this_end, zx_koid_t* this_end_koid,
                              zx_koid_t* that_end_koid) {
  ZX_DEBUG_ASSERT(this_end_koid);
  ZX_DEBUG_ASSERT(that_end_koid);
  zx_info_handle_basic_t token_info{};
  size_t actual_count = 0;
  size_t avail_count = 0;
  zx_status_t get_info_status = this_end.get_info(ZX_INFO_HANDLE_BASIC, &token_info,
                                                  sizeof(token_info), &actual_count, &avail_count);
  if (get_info_status != ZX_OK) {
    return get_info_status;
  }
  ZX_DEBUG_ASSERT(actual_count == 1);
  ZX_DEBUG_ASSERT(avail_count == 1);
  if (token_info.type != ZX_OBJ_TYPE_CHANNEL) {
    return ZX_ERR_WRONG_TYPE;
  }
  ZX_DEBUG_ASSERT(token_info.koid != 0);
  ZX_DEBUG_ASSERT(token_info.related_koid != 0);
  *this_end_koid = token_info.koid;
  *that_end_koid = token_info.related_koid;
  return ZX_OK;
}

}  // namespace sysmem_driver
