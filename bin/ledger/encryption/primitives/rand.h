// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_RAND_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_RAND_H_

#include <cstdint>

#include <stddef.h>

namespace encryption {

// Fills buffer with |size| random bytes.
void RandBytes(void* buffer, size_t size);

// Returns a random number in range [0, UINT64_MAX].
uint64_t RandUint64();

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_RAND_H_
