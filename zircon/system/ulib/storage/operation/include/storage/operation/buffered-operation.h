// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_OPERATION_BUFFERED_OPERATION_H_
#define STORAGE_OPERATION_BUFFERED_OPERATION_H_

#include <lib/zx/vmo.h>
#include <zircon/device/block.h>

#include <fbl/vector.h>
#include <storage/operation/operation.h>

namespace storage {

// An operation paired with a source vmo.
//
// Used to indicate a request to move in-memory data to an on-disk location,
// or vice versa. To be transmitted to storage, the |vmo| must be later
// converted to a "vmoid_t" object.
struct UnbufferedOperation {
  zx::unowned_vmo vmo;
  Operation op;
};

// An operation paired with a source vmoid.
//
// This vmoid is a token that represents a buffer that is attached to the
// underlying storage device.
struct BufferedOperation {
  vmoid_t vmoid;
  Operation op;
};

// Sums the |length| of all requests.
uint64_t BlockCount(const fbl::Vector<UnbufferedOperation>& requests);

}  // namespace storage

#endif  // STORAGE_OPERATION_BUFFERED_OPERATION_H_
