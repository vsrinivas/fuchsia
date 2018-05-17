// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <vector>
#include "internal_list_fuzzer_helpers.h"

using namespace overnet;

namespace {
class InputStream {
 public:
  InputStream(const uint8_t* data, size_t size)
      : cur_(data), end_(data + size) {}

  uint8_t NextByte() {
    if (cur_ == end_) return 0;
    return *cur_++;
  }

  uint16_t NextShort() {
    uint16_t x = NextByte();
    x <<= 8;
    x |= NextByte();
    return x;
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  internal_list::Fuzzer fuzzer;

  for (;;) {
    fuzzer.Verify();
    uint8_t op_list = input.NextByte();
    uint8_t op = op_list & 0x0f;
    uint8_t list = op_list >> 4;
    uint8_t node = input.NextByte();
    switch (op) {
      default:
        return 0;
      case 1:
        if (!fuzzer.PushBack(node, list)) return 0;
        break;
      case 2:
        if (!fuzzer.PushFront(node, list)) return 0;
        break;
      case 3:
        if (!fuzzer.Remove(node, list)) return 0;
        break;
    }
  }
}
