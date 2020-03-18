// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/reader.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::vector<uint8_t> buf(data, data + size);

  inspect::ReadFromBuffer(std::move(buf));

  return 0;
}
