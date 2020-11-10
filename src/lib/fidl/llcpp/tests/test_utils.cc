// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl/llcpp/tests/test_utils.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace llcpp_conformance_utils {

// TODO(fxbug.dev/63900): Remove this when rights are specified in GIDL.
std::vector<zx_handle_info_t> ToHandleInfoVec(std::vector<zx_handle_t> handles) {
  std::vector<zx_handle_info_t> infos(handles.size());
  for (size_t i = 0; i < handles.size(); i++) {
    infos[i] = zx_handle_info_t{
        .handle = handles[i],
        .type = ZX_OBJ_TYPE_NONE,
        .rights = ZX_RIGHT_SAME_RIGHTS,
    };
  }
  return infos;
}

}  // namespace llcpp_conformance_utils
