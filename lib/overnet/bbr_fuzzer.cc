// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bbr_fuzzer_helpers.h"
#include "varint.h"

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

  uint64_t Next64() {
    uint64_t out;
    if (!varint::Read(&cur_, end_, &out)) out = 0;
    return out;
  }

  std::vector<uint64_t> NextBlock() {
    std::vector<uint64_t> out;
    while (uint64_t x = Next64()) {
      out.push_back(x);
    }
    return out;
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  auto start_time = input.Next64();
  auto mss = input.Next64();
  auto srtt = input.Next64();
  BBRFuzzer fuzzer(start_time, mss, srtt);

  for (;;) {
    switch (input.NextByte()) {
      default:
        return 0;
      case 1:
        fuzzer.IncTime(input.Next64());
        break;
      case 2: {
        auto size = input.Next64();
        auto send_delay = input.Next64();
        fuzzer.Transmit(size, send_delay);
      } break;
      case 3: {
        auto acks = input.NextBlock();
        auto nacks = input.NextBlock();
        fuzzer.Ack(acks, nacks);
      } break;
    }
  }
}
