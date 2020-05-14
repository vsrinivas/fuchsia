// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_FW_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_FW_H_

#include <zircon/types.h>

namespace metadata {

constexpr size_t kMaxNameLen = 16;

struct Firmware {
  char name[kMaxNameLen];
  uint8_t id;
  zx_paddr_t pa;
};

}  // namespace metadata

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_FW_H_
