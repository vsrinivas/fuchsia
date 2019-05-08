// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/vmo.h>
#include <zircon/device/block.h>

namespace blobfs {

enum class OperationType {
    kRead,
    kWrite,
    kTrim, // Unimplemented.
};

// A mapping of an in-memory buffer to an on-disk location.
//
// All units are in Blobfs blocks.
struct Operation {
    OperationType type;
    uint64_t vmo_offset = 0;
    uint64_t dev_offset = 0;
    uint64_t length = 0;
};

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

} // namespace blobfs
