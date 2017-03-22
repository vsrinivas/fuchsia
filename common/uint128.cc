// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uint128.h"

#include <endian.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace bluetooth {
namespace common {

UInt128::UInt128() {
  bytes_.fill(0);
}

UInt128::UInt128(std::initializer_list<uint8_t> bytes) {
  FTL_DCHECK(bytes.size() <= bytes_.size());
  bytes_.fill(0);
  std::copy(bytes.begin(), bytes.end(), bytes_.begin());
}

}  // namespace common
}  // namespace bluetooth
