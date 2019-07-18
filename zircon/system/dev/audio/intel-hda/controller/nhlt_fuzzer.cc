// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

#include <fbl/span.h>

#include "nhlt.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  (void)audio::intel_hda::Nhlt::FromBuffer(fbl::Span<const uint8_t>(data, size));
  return 0;
}
