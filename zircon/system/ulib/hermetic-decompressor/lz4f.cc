// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"

#include <cassert>
#include <lz4/lz4frame.h>

int64_t DecompressorEngine::operator()(byte_view input, std::byte* output, size_t output_size) {
  LZ4F_decompressionContext_t ctx;
  auto ret = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
  if (LZ4F_isError(ret)) {
    return ZX_ERR_BAD_STATE;
  }

  static constexpr const LZ4F_decompressOptions_t kDecompressOpt{};

  size_t nread = input.size(), nwritten = output_size;
  ret = LZ4F_decompress(ctx, output, &nwritten, input.data(), &nread, &kDecompressOpt);
  if (ret != 0 || nread != input.size() || nwritten != output_size) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  return ZX_OK;
}
