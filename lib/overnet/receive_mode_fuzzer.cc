// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>
#include "receive_mode_fuzzer_helpers.h"
#include "varint.h"

using namespace overnet;

namespace {
class InputStream {
 public:
  InputStream(const uint8_t* data, size_t size)
      : cur_(data), end_(data + size) {}

  uint64_t Next64() {
    uint64_t out;
    if (!varint::Read(&cur_, end_, &out)) out = 0;
    return out;
  }

  uint8_t NextByte() {
    if (cur_ == end_) return 0;
    return *cur_++;
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  receive_mode::Fuzzer fuzzer(input.NextByte());
  for (;;) {
    fuzzer.Step();
    switch (input.NextByte()) {
      default:
        // input exhausted, or unknown op-code
        return 0;
      case 1:
        if (!fuzzer.Begin(input.Next64())) return 0;
        break;
      case 2: {
        uint64_t seq = input.Next64();
        uint8_t status = input.NextByte();
        if (!fuzzer.Completed(seq, status)) return 0;
      } break;
    }
  }
}
