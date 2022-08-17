// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koid_util.h"

#include <lib/zx/process.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "macros.h"

namespace sysmem_driver {

zx_status_t get_handle_koids(const zx::object_base& this_end, zx_koid_t* this_end_koid,
                             zx_koid_t* that_end_koid, zx_obj_type_t type) {
  ZX_DEBUG_ASSERT(this_end_koid);
  ZX_DEBUG_ASSERT(that_end_koid);
  zx_info_handle_basic_t basic_info{};
  size_t actual_count = 0;
  size_t avail_count = 0;
  zx_status_t get_info_status = this_end.get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                  sizeof(basic_info), &actual_count, &avail_count);
  if (get_info_status != ZX_OK) {
    return get_info_status;
  }
  ZX_DEBUG_ASSERT(actual_count == 1);
  ZX_DEBUG_ASSERT(avail_count == 1);
  if (basic_info.type != type) {
    return ZX_ERR_WRONG_TYPE;
  }
  ZX_DEBUG_ASSERT(basic_info.koid != 0);
  // We only care about channel and eventpair so far, but more could be added as needed.
  ZX_DEBUG_ASSERT(basic_info.related_koid != 0 ||
                  (type != ZX_OBJ_TYPE_CHANNEL && type != ZX_OBJ_TYPE_EVENTPAIR));
  *this_end_koid = basic_info.koid;
  *that_end_koid = basic_info.related_koid;
  return ZX_OK;
}

}  // namespace sysmem_driver
