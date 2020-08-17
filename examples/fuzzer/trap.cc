// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* A simple fuzzer that finds a trivial crash via a __builtin_trap. */
#include <stdint.h>
#include <stddef.h>

int foo_function(const uint8_t *data, size_t size) {
  int fizzle = 1 + size;
  if (fizzle < 1) {
    return 0;
  }
  if (size > 0 && data[0] == 'H') {
    if (size > 1 && data[1] == 'I') {
      if (size > 2 && data[2] == '!') {
         __builtin_trap();
      }
    }
  }
  return 0;
}

int bar_function(const uint8_t *data, size_t size) {
  int fizzle = 1 + size;
  if (fizzle < 1) {
      return 0;
  }
  return foo_function(data, size);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return bar_function(data, size);
}
