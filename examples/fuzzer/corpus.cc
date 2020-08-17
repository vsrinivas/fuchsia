// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* A simple fuzzer that finds a trivial crash via a __builtin_trap. */
#include <stdint.h>
#include <stddef.h>

int f_function(const uint8_t *data, size_t size) {
  __builtin_trap();
}

int e_function(const uint8_t *data, size_t size) {
  if (size > 4 && data[4] == 'e') {
    return f_function(data, size);
  }
  return 0;
}

int d_function(const uint8_t *data, size_t size) {
  if (size > 3 && data[3] == 'd') {
    return e_function(data, size);
  }
  return 0;
}

int c_function(const uint8_t *data, size_t size) {
  if (size > 2 && data[2] == 'c') {
    return d_function(data, size);
  }
  return 0;
}

int b_function(const uint8_t *data, size_t size) {
  if (size > 1 && data[1] == 'b') {
    return c_function(data, size);
  }
  return 0;
}

int a_function(const uint8_t *data, size_t size) {
  if (size > 0 && data[0] == 'a') {
    return b_function(data, size);
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return a_function(data, size);
}
