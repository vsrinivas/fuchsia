// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/fidl.h>

#include <cstdint>
#include <vector>

namespace fidl {
namespace test {
namespace util {

std::vector<zx_handle_info_t> ToHandleInfos(std::vector<zx_handle_t> handles) {
  std::vector<zx_handle_info_t> handle_infos;
  for (zx_handle_t handle : handles) {
    handle_infos.push_back(zx_handle_info_t{
        .handle = handle,
        .type = ZX_OBJ_TYPE_NONE,
        .rights = ZX_RIGHT_SAME_RIGHTS,
        .unused = 0,
    });
  }
  return handle_infos;
}

}  // namespace util
}  // namespace test
}  // namespace fidl
