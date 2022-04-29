// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoder_fuzzer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  VaapiFuzzerTestFixture fixture;
  fixture.SetUp();
  fixture.RunFuzzer("video/vp9", data, size);
  fixture.TearDown();
  return 0;
}
