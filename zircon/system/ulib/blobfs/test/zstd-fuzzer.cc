// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "compression/zstd-plain.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  size_t compressed_size = size;
  size_t uncompressed_size = size * 2;
  std::vector<uint8_t> uncompressed_buf(uncompressed_size);
  blobfs::ZSTDDecompressor decompressor;

  decompressor.Decompress(uncompressed_buf.data(), &uncompressed_size, data, compressed_size);
  return 0;
}
