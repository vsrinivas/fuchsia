// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/output_producer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format/constants.h"

namespace media::audio {
namespace {

using testing::Each;
using testing::Eq;
using testing::FloatEq;
using testing::Pointwise;

// Testing the OutputProducer means validating bit-for-bit from our float32 accumulator to the
// float-based output format, as well as appropriate rounding behavior when we output to other
// output formats (unsigned int8, int16, most-significant-justified-int24-in-32).

// Note that 32-bit floats have 25 effective bits of precision: 1 sign bit and 24 mantissa (23
// explicit, 1 implicit: https://en.wikipedia.org/wiki/Single-precision_floating-point_format).
// Specifying test input values as floating-point is not easily readable. Instead, when a float
// value must be precisely specified to 25-bit resolution, we use hexadecimal integers, then
// normalize to [-1.0, 1.0]. For best readability (considering 4-bits-per-hexadecimal-digit) we use
// seven hexadecimal digits (most-significant-justified).
//
// Why not use int32? 2 reasons: 1) int32 cannot represent the valid float value "+1.0", and
// 2) int32 cannot represent out-of-range values (these are possible with our mix pipeline).
//
class OutputProducerTest : public testing::Test {
 public:
  std::unique_ptr<OutputProducer> SelectOutputProducer(
      fuchsia::media::AudioSampleFormat dest_format, uint32_t num_channels) {
    fuchsia::media::AudioStreamType dest_details;
    dest_details.sample_format = dest_format;
    dest_details.channels = num_channels;
    dest_details.frames_per_second = 48000;

    return OutputProducer::Select(dest_details);
  }

  // When we specify source data in uint8/int16/int32 formats, it improves readability to specify
  // expected values in that format as well. The expected array itself is float[], so we use this
  // function to shift values expressed as uint8, int16, etc., into the [-1.0, 1.0] float range.
  //
  // Note: 'shift_by' values must be 1 less than might seem obvious, to account for the sign bit.
  // E.g.: to shift int16 values -0x8000 and 0x7FFF into float range, shift_by must be 15 (not 16).
  void ShiftRightBy(std::vector<float>& floats, uint32_t shift_by) {
    for (float& val : floats) {
      for (auto shift_num = 0u; shift_num < shift_by; ++shift_num) {
        val *= 0.5f;
      }
    }
  }
};

// Create OutputProducer objects for outgoing buffers of type uint8
TEST_F(OutputProducerTest, DataFormat_8) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2));
}

// Create OutputProducer objects for outgoing buffers of type int16
TEST_F(OutputProducerTest, DataFormat_16) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 4));
}

// Create OutputProducer objects for outgoing buffers of type int24-in-32
TEST_F(OutputProducerTest, DataFormat_24) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 3));
}

// Create OutputProducer objects for outgoing buffers of type float
TEST_F(OutputProducerTest, DataFormat_Float) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1));
}

// Are all valid data values rounded correctly to 8-bit outputs?
TEST_F(OutputProducerTest, PassThru_8) {
  auto accum = std::vector<float>{
      // clang-format off
      -0x898989, // clamped to uint8 min 0x00
      -0x800000, // becomes 0x00, uint8 min
      -0x408080, // becomes 0x3F, the -0x0.808 rounded out (down)
      -0x000111, // becomes 0x80, -0x0.0111 rounded in (up)
              0, // becomes 0x80
       0x408080, // becomes 0xC1, 0x0.808 rounded out (up)
       0x7FFFFF, // becomes 0xFF, uint8 max
       0x898989, // clamped to uint8 max 0xFF
      // clang-format on
  };
  ShiftRightBy(accum, 23);

  auto dest = std::vector<uint8_t>{12, 23, 34, 45, 56, 67, 78, 89, 42};  // overwritten, except last
  // the "becomes" values mentioned above, plus the final not-overwritten value of dest.
  auto expect = std::vector<uint8_t>{0x00, 0x00, 0x3F, 0x80, 0x80, 0xC1, 0xFF, 0xFF, 42};
  ASSERT_EQ(dest.size(), accum.size() + 1) << "Test depends on dest being 1 longer than accum";
  ASSERT_EQ(expect.size(), accum.size() + 1) << "Test depends on expect being 1 longer than accum";

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size());
  EXPECT_THAT(dest, testing::Pointwise(testing::FloatEq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs?
TEST_F(OutputProducerTest, PassThru_16) {
  auto accum = std::vector<float>{
      // clang-format off
      -0x898989, // clamped to int16 min -0x8000
      -0x800000, // becomes -0x8000, int16 min
      -0x408080, // becomes -0x4081, we round -0x0.80 out (down)
      -0x000111, // becomes -0x0001, the -0x0.11 rounded in (up)
              0, // becomes 0x0000
       0x408080, // becomes 0x4081, we round 0x0.8 out (up)
       0x7FFFFF, // becomes 0x7FFF, int16 max
       0x898989, // clamped to int16 max 0x7FFF
      // clang-format on
  };
  ShiftRightBy(accum, 23);

  auto dest = std::vector<int16_t>{0123, 1234, 2345, 3456, 4567, 5678, 6789, 7890, -42};
  // the "becomes" values mentioned above, plus the final not-overwritten value of dest.
  auto expect = std::vector<int16_t>{-0x8000, -0x8000, -0x4081, -1, 0, 0x4081, 0x7FFF, 0x7FFF, -42};

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size() / 2);
  EXPECT_THAT(dest, testing::Pointwise(testing::FloatEq(), expect));
}

// Are all valid data values passed correctly to 24-bit outputs?
// int24-in-32 has 1 fewer bit than float32 so we add a least-significant hex digit to specify
// values to be rounded. The additional bit (the final 0x08 below) is the equivalent of .5 or 0
TEST_F(OutputProducerTest, PassThru_24) {
  auto dest = std::vector<int32_t>{99, 99, 99, 99, 99, 99, 99, 99, -42};  // to be overwritten
  auto accum = std::vector<float>{
      // clang-format off
      -0x8989890, // clamped to int24/32 min -0x80000000
      -0x8000000, // becomes -0x80000000, int24/32 min
      -0x4080808, // becomes -0x40808100, we round -0x0.8 out (down)
      -0x0001110, // becomes -0x00011100
               0, // becomes  0x00000000
       0x4080808, // becomes  0x40808100, we round 0x0.8 out (up)
       0x7FFFFF0, // becomes  0x7FFFFF00, int24-in-32 max
       0x8989890, // clamped to int24-in-32 max 0x7FFFFF00
      // clang-format on
  };
  ShiftRightBy(accum, 27);  // shift by seven hex digits (minus the sign bit, as always)

  // the "becomes" values mentioned above, plus the final not-overwritten dest.
  auto expect = std::vector<int32_t>{
      // clang-format off
      kMinInt24In32,
      kMinInt24In32,
        -0x40808100,
        -0x00011100,
                  0,
         0x40808100,
      kMaxInt24In32,
      kMaxInt24In32,
                -42,
      // clang-format on
  };

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 4);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size() / 4);
  EXPECT_THAT(dest, testing::Pointwise(testing::FloatEq(), expect));
}

// Are all valid data values passed correctly to float outputs
TEST_F(OutputProducerTest, PassThru_Float) {
  auto accum = std::vector<float>{
      -1.1, 1.1, -1.0, 1.0, -0.503921568, 0.503921568, -0.000000119, 0.000000119, 0, NAN,
  };
  auto dest = std::vector<float>(accum.size());
  std::fill(dest.begin(), dest.end(), 4.2);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size() - 1);
  // Update the 3 places where accum should differ from dest, so we can compare entire arrays.
  accum[0] = -1.0;                // value was clamped
  accum[1] = 1.0;                 // value was clamped
  accum[accum.size() - 1] = 4.2;  // previous not-overwritten dest value
  EXPECT_THAT(accum, testing::Pointwise(testing::FloatEq(), dest));
}

// Are 8-bit output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, PassThru_8_Silence) {
  constexpr auto num_silent_samples = 6;
  std::array<uint8_t, num_silent_samples + 1> dest;
  std::fill(dest.begin(), dest.end(), 0xFF);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2);
  ASSERT_NE(nullptr, output_producer);

  output_producer->FillWithSilence(dest.data(), num_silent_samples / 2);
  // Check the not-overwritten dest value, then clear it so we can compare entire arrays
  EXPECT_EQ(dest[num_silent_samples], 255);
  dest[num_silent_samples] = 0x80;
  EXPECT_THAT(dest, testing::Each(testing::Eq(0x80)));
}

// Are 16-bit output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, PassThru_16_Silence) {
  constexpr auto num_silent_samples = 6;
  std::array<int16_t, num_silent_samples + 1> dest;
  std::fill(dest.begin(), dest.end(), 9876);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 3);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_silent_samples / 3);
  // Check the not-overwritten dest value, then clear it so we can compare entire arrays
  EXPECT_EQ(dest[num_silent_samples], 9876);
  dest[num_silent_samples] = 0;
  EXPECT_THAT(dest, testing::Each(testing::Eq(0)));
}

// Are 24-bit output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, PassThru_24_Silence) {
  constexpr auto num_silent_samples = 6;

  std::array<int32_t, num_silent_samples + 1> dest;
  std::fill(dest.begin(), dest.end(), 0x12345678);

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_silent_samples);
  // Check the not-overwritten dest value, then clear it so we can compare entire arrays
  EXPECT_EQ(dest[num_silent_samples], 0x12345678);
  dest[num_silent_samples] = 0;
  EXPECT_THAT(dest, testing::Each(testing::Eq(0)));
}

// Are float output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, PassThru_Float_Silence) {
  constexpr auto num_silent_samples = 6;
  std::array<float, num_silent_samples + 1> dest;
  std::fill(dest.begin(), dest.end(), -4.2f);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 2);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_silent_samples / 2);
  // Check the not-overwritten dest value, then clear it so we can compare entire arrays
  EXPECT_EQ(dest[num_silent_samples], -4.2f);
  dest[num_silent_samples] = 0.0f;
  EXPECT_THAT(dest, testing::Each(testing::Eq(0.0f)));
}

}  // namespace
}  // namespace media::audio
