// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/decompressor.h"

#include <zircon/errors.h>

#include <memory>

#include "src/storage/blobfs/compression/chunked.h"

namespace blobfs {

zx_status_t Decompressor::Create(CompressionAlgorithm algorithm,
                                 std::unique_ptr<Decompressor>* out) {
  switch (algorithm) {
    case CompressionAlgorithm::kChunked:
      *out = std::make_unique<ChunkedDecompressor>();
      return ZX_OK;
    case CompressionAlgorithm::kUncompressed:
      ZX_DEBUG_ASSERT(false);
      return ZX_ERR_NOT_SUPPORTED;
  }

  ZX_DEBUG_ASSERT(false);
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace blobfs
