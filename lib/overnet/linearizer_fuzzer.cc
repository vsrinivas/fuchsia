// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>
#include "linearizer_fuzzer_helpers.h"

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

  const uint8_t* Block(size_t bytes) {
    if (bytes > end_ - cur_) {
      tail_.clear();
      tail_.resize(bytes, 0);
      memcpy(tail_.data(), cur_, end_ - cur_);
      cur_ = end_;
      return tail_.data();
    }
    const uint8_t* out = cur_;
    cur_ += bytes;
    return out;
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
  std::vector<uint8_t> tail_;
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  linearizer_fuzzer::LinearizerFuzzer fuzzer;

  for (;;) {
    uint8_t op = input.NextByte();
    if (op == 0) {
      return 0;
    } else if (op == 1) {
      fuzzer.Close(input.NextByte());
    } else if (op == 2) {
      fuzzer.Pull();
    } else {
      uint8_t len = op - 2;
      assert(len >= 1);
      bool eom = (len & 1) != 0;
      len >>= 1;
      uint16_t offset = input.NextShort();
      fuzzer.Push(offset, len, eom, input.Block(len));
    }
  }
}
