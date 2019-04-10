// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_METADATA_FW_H_
#define DDKTL_METADATA_FW_H_

#include <zircon/types.h>

namespace metadata {

constexpr size_t kMaxNameLen = 16;

struct Firmware {
    char name[kMaxNameLen];
    uint8_t id;
    zx_paddr_t pa;
};

} // namespace metadata

#endif  // DDKTL_METADATA_FW_H_
