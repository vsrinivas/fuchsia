// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "compression/lz4.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  size_t src_size = size;
  size_t target_size = size * 2;
  std::vector<uint8_t> target_buffer(target_size);

  blobfs::LZ4Decompress(target_buffer.data(), &target_size, data, &src_size);
  return 0;
}
