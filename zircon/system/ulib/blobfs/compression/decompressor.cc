// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decompressor.h"

#include <zircon/errors.h>

#include <memory>

#include "lz4.h"
#include "zstd-plain.h"
#include "zstd-seekable.h"

namespace blobfs {

zx_status_t Decompressor::Create(CompressionAlgorithm algorithm,
                                 std::unique_ptr<Decompressor>* out) {
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      *out = std::make_unique<LZ4Decompressor>();
      break;
    case CompressionAlgorithm::ZSTD:
      *out = std::make_unique<ZSTDDecompressor>();
      break;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      *out = std::make_unique<ZSTDSeekableDecompressor>();
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

}  // namespace blobfs
