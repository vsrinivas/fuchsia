// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/protocol/coding.h"

using namespace overnet;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto ignore = [](auto) {};
  ignore(Decode(Slice::FromCopiedBuffer(data, size)));
  return 0;
}
