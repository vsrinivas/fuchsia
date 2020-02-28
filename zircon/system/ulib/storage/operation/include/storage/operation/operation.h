// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_OPERATION_OPERATION_H_
#define STORAGE_OPERATION_OPERATION_H_

#include <zircon/device/block.h>

#include <cstdint>

namespace storage {

enum class OperationType {
  kRead,
  kWrite,
  kTrim,              // Unimplemented.
  kMaxValue = kTrim,  // For FuzzedDataProvider
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

// An operation paired with a source vmoid.
//
// This vmoid is a token that represents a buffer that is attached to the
// underlying storage device.
//
// Even though a vmoid is a Fuchsia concept, nothing in this file prevents host
// side code to include this code... it should just ignore the vmoid value and
// look for the data somewhere else.
struct BufferedOperation {
  vmoid_t vmoid;
  Operation op;
};

}  // namespace storage

#endif  // STORAGE_OPERATION_OPERATION_H_
