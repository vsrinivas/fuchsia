// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

// A simple fuzzer that can be used in integration tests.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 0 && data[0] == 'F') {
    if (size > 1 && data[1] == 'U') {
      if (size > 2 && data[2] == 'Z') {
        if (size > 3 && data[3] == 'Z') {
          __builtin_trap();
        }
      }
    }
  }
  return 0;
}
