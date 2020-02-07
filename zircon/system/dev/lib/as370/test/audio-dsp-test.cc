// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <soc/as370/audio-dsp.h>
#include <zxtest/zxtest.h>

namespace audio {

TEST(CicFilter, OnesFilled) {
  CicFilter filter;
  std::array<uint8_t, 0x40> in;
  std::array<uint8_t, 0x10>
      out;  // every 64 bits in, generate 32 bits that are converted to 16 bits.
  in.fill(0xff);
  out.fill(0);
  constexpr uint32_t output_channel = 0;
  filter.Filter(0, in.begin(), in.size(), out.begin(), 2, 0, 2, 0, output_channel);
  uint16_t expected[] = {0x009f, 0x0000, 0x0f54, 0x0000, 0x3280, 0x0000, 0x3f8b, 0x0000};
  EXPECT_BYTES_EQ(out.begin(), expected, out.size());
}

TEST(CicFilter, OnesFilledAmplified) {
  CicFilter filter;
  std::array<uint8_t, 0x40> in;
  std::array<uint8_t, 0x10>
      out;  // every 64 bits in, generate 32 bits that are converted to 16 bits.
  in.fill(0xff);
  out.fill(0);
  constexpr uint32_t output_channel = 1;
  constexpr uint32_t multiplication_shift = 4;
  filter.Filter(0, in.begin(), in.size(), out.begin(), 2, 0, 2, output_channel,
                multiplication_shift);
  uint16_t expected[] = {0x0000, 0x09f0, 0x0000, 0x7fff, 0x0000, 0x7fff, 0x0000, 0x7fff};
  EXPECT_BYTES_EQ(out.begin(), expected, out.size());
}

TEST(CicFilter, ZerosAndOnesEqual) {
  CicFilter filter;
  std::array<uint8_t, 0x40> in;
  std::array<uint8_t, 0x10>
      out;        // every 64 bits in, generate 32 bits that are converted to 16 bits.
  in.fill(0x55);  // alternate ones and zeros to get small out values.
  out.fill(0);
  constexpr uint32_t output_channel = 0;
  filter.Filter(0, in.begin(), in.size(), out.begin(), 2, 0, 2, 0, output_channel);
  uint16_t expected[] = {0x0006, 0x0000, 0x003c, 0x0000, 0x0038, 0x0000, 0x0004, 0x0000};
  EXPECT_BYTES_EQ(out.begin(), expected, out.size());
}

}  // namespace audio
