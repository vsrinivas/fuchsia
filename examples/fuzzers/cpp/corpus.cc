// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A fuzzer that uses a corpus to find a harder-to-find crash (simulated by __builtin_trap()).

#include <stddef.h>
#include <stdint.h>

#include <bitset>

// The code under test. Normally this would be in a separate library.
namespace {

// To reach the "crash", inputs must start with 7 bytes of the sequence b11, b101, b1001, etc.
// The checks on this sequence are done in way to make it harder for the fuzzer to infer the
// necessary sequence, and thus to demonstrate the usefulness of a seed corpus containing that
// sequence.
void crasher(const uint8_t *data, size_t size) {
  uint8_t prev = 0;
  for (size_t i = 0; i < 7; ++i) {
    if (i == size || std::bitset<8>(data[i]).count() != 2 || data[i] <= prev || data[i] % 2 == 0) {
      return;
    }
    prev = data[i];
  }
  if (size > 7 && data[7] == 'H') {
    if (size > 8 && data[8] == 'I') {
      if (size > 9 && data[9] == '!') {
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
