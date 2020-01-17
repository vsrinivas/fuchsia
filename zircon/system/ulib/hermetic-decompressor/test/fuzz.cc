// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-decompressor/hermetic-decompressor.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kMaxSize = 0x1000000;
static_assert(kMaxSize % PAGE_SIZE == 0, "kMaxSize must be page-aligned.");

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static zx::vmo compressed;
  if (!compressed.is_valid() && zx::vmo::create(kMaxSize, 0, &compressed) != ZX_OK) {
    abort();
  }
  static zx::vmo output;
  if (!output.is_valid() && zx::vmo::create(kMaxSize, 0, &output) != ZX_OK) {
    abort();
  }
  if (size > kMaxSize || output.write(data, 0, size) != ZX_OK) {
    return 0;
  }
  HermeticDecompressor()(compressed, 0, size, output, 0, kMaxSize);
  return 0;
}
