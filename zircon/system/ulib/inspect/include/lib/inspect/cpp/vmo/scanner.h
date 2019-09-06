// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_SCANNER_H_
#define LIB_INSPECT_CPP_VMO_SCANNER_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <zircon/types.h>

namespace inspect {
namespace internal {

// Read blocks out of the buffer.
//
// For each block that it found, this function calls the callback function
// with the block's index and a pointer to the block. If the callback
// returns false, it will not be called again.
//
// Returns ZX_OK if the buffer was valid and successfully loaded,
// otherwise returns an error describing what went wrong.
zx_status_t ScanBlocks(const uint8_t* buffer, size_t size,
                       fit::function<bool(BlockIndex, const Block*)> callback);

}  // namespace internal
}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_SCANNER_H_
