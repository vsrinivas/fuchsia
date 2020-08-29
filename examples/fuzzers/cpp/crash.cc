// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

// A simple fuzzer that finds a trivial crash (simulated by __builtin_trap()).

// The code under test. Normally this would be in a separate library.
namespace {

void crasher(const uint8_t *data, size_t size) {
  if (size > 0 && data[0] == 'H') {
    if (size > 1 && data[1] == 'I') {
      if (size > 2 && data[2] == '!') {
        __builtin_trap();
      }
    }
  }
}

}  // namespace

// The fuzz target function
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  crasher(data, size);
  return 0;
}
