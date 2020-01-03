// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <storage/operation/buffered_operation.h>

namespace storage {

uint64_t BlockCount(const fbl::Vector<UnbufferedOperation>& operations) {
  uint64_t total_length = 0;
  for (const auto& operation : operations) {
    total_length += operation.op.length;
  }
  return total_length;
}

}  // namespace storage
