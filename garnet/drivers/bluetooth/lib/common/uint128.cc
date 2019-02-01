// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uint128.h"

#include <zircon/syscalls.h>

namespace btlib {
namespace common {

UInt128 RandomUInt128() {
  UInt128 value;
  zx_cprng_draw(value.data(), value.size());
  return value;
}

}  // namespace common
}  // namespace btlib
