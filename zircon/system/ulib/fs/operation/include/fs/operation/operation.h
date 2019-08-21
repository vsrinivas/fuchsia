// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace fs {

enum class OperationType {
  kRead,
  kWrite,
  kTrim,  // Unimplemented.
};

// A mapping of an in-memory buffer to an on-disk location.
//
// All units are in filesystem-size blocks.
struct Operation {
  OperationType type;
  uint64_t vmo_offset = 0;
  uint64_t dev_offset = 0;
  uint64_t length = 0;
};

}  // namespace fs
