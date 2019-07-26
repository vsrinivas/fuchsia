// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include <lib/edid/edid.h>

// fuzz_target.cc
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  edid::Edid test;
  const char* err_msg;
  if (size < UINT16_MAX) {
    test.Init(data, static_cast<uint16_t>(size), &err_msg);
  }
  return 0;
}
