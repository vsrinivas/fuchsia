// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/protocol/coding.h"

using namespace overnet;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0)
    return 0;
  auto coding = static_cast<Coding>(data[0]);
  auto input = Slice::FromCopiedBuffer(data + 1, size - 1);
  auto encoded = Encode(coding, input);
  if (encoded.is_error())
    return 0;
  auto decoded = Decode(*encoded);
  assert(decoded.is_ok());
  assert(input == *decoded);
  return 0;
}
