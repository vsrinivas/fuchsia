// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <safemath/checked_math.h>
#include <storage/operation/unbuffered_operation.h>

namespace storage {

uint64_t BlockCount(fbl::Span<const UnbufferedOperation> operations) {
  safemath::CheckedNumeric<uint64_t> total_length = 0;
  for (const auto& operation : operations) {
    total_length += operation.op.length;
  }
  return total_length.ValueOrDie();
}

}  // namespace storage
