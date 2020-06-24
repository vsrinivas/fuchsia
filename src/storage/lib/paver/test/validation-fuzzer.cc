// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/span.h>

#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/validation.h"

// Simple fuzzer to detect invalid memory accesses of validation.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  (void)paver::IsValidKernelZbi(paver::Arch::kArm64, fbl::Span<const uint8_t>(data, size));
  return 0;
}
