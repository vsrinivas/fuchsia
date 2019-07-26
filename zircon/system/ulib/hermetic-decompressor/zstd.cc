// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"

#include <zstd/zstd.h>

int64_t DecompressorEngine::operator()(byte_view input, std::byte* output, size_t output_size) {
  auto rc = ZSTD_decompress(output, output_size, input.data(), input.size());
  if (ZSTD_isError(rc) || rc != output_size) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}
