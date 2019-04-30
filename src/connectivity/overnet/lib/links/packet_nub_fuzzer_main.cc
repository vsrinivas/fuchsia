// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_nub_fuzzer.h"

using namespace overnet;

namespace {
class InputStream {
 public:
  InputStream(const uint8_t* data, size_t size)
      : cur_(data), end_(data + size) {}

  uint64_t Next64() {
    uint64_t out;
    if (!varint::Read(&cur_, end_, &out))
      out = 0;
    return out;
  }

  uint8_t NextByte() {
    if (cur_ == end_)
      return 0;
    return *cur_++;
  }

  Slice NextSlice() {
    auto len = NextByte();
    auto pad = NextByte();
    return Slice::WithInitializerAndBorders(
        len + pad, Border::None(), [this, len, pad](uint8_t* p) {
          for (uint64_t i = 0; i < len; i++) {
            *p++ = NextByte();
          }
          for (uint64_t i = 0; i < pad; i++) {
            *p++ = 0;
          }
        });
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  PacketNubFuzzer fuzzer(false);
  for (;;) {
    switch (input.NextByte()) {
      default:
        // input exhausted, or unknown op-code
        return 0;
      case 1: {
        if (!fuzzer.StepTime(input.Next64()))
          return 0;
      } break;
      case 2: {
        auto src = input.Next64();
        auto slice = input.NextSlice();
        fuzzer.Process(src, std::move(slice));
      } break;
    }
  }
}
