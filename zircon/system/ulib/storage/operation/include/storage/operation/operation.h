// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_OPERATION_OPERATION_H_
#define STORAGE_OPERATION_OPERATION_H_

#include <zircon/device/block.h>

#include <cstdint>

#include <fbl/span.h>

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

// An operation paired with a source vmoid or a pointer to the data for host side code.
//
// This vmoid is a token that represents a buffer that is attached to the
// underlying storage device.
struct BufferedOperation {
#ifdef __Fuchsia__
  vmoid_t vmoid;
#else
  void* data;
#endif
  Operation op;
};

// Sums the |length| of all requests. It will assert if overflow occurs; the caller is responsible
// for making sure this does not happen.
uint64_t BlockCount(fbl::Span<const BufferedOperation> operations);

}  // namespace storage

#endif  // STORAGE_OPERATION_OPERATION_H_
