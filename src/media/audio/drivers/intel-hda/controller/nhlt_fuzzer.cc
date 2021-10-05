// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/span.h>

#include <cstddef>
#include <cstdint>

#include "nhlt.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  (void)audio::intel_hda::Nhlt::FromBuffer(cpp20::span<const uint8_t>(data, size));
  return 0;
}
