// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_protocol_fuzzer_helpers.h"

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

  Slice NextSlice() {
    auto len = NextByte();
    auto prefix = NextByte();
    return Slice::WithInitializerAndPrefix(len, prefix,
                                           [this, len](uint8_t* p) {
                                             for (uint8_t i = 0; i < len; i++) {
                                               *p++ = NextByte();
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
  PacketProtocolFuzzer fuzzer;
  for (;;) {
    switch (input.NextByte()) {
      default:
        // input exhausted, or unknown op-code
        return 0;
      case 1: {
        auto sender = input.NextByte();
        auto slice = input.NextSlice();
        if (!fuzzer.BeginSend(sender, slice)) return 0;
      } break;
      case 2: {
        auto sender = input.NextByte();
        auto send = input.Next64();
        auto status = input.NextByte();
        if (!fuzzer.CompleteSend(sender, send, status)) return 0;
      } break;
      case 3:
        if (!fuzzer.StepTime(input.Next64())) return 0;
        break;
    }
  }
}
