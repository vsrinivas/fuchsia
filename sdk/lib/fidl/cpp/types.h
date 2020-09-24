// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TYPES_H_
#define LIB_FIDL_CPP_TYPES_H_

#include <vector>

#ifdef __Fuchsia__
#include <lib/zx/handle.h>
#endif

namespace fidl {

// This is used to store unknown data for non-resource union types. It contains
// a single field that matches the UnknownData struct so that codegen is simpler
// (.bytes can be used for both resource and non-resource unions)
struct UnknownBytes {
  std::vector<uint8_t> bytes;
};

#ifdef __Fuchsia__
struct UnknownData {
  std::vector<uint8_t> bytes;
  std::vector<zx::handle> handles;
};
#endif

}  // namespace fidl

#endif  // LIB_FIDL_CPP_TYPES_H_
