// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/output_producer.h"

#include <cmath>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/sample_converter.h"

namespace media::audio {
namespace {

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
// 2) int32 cannot represent out-of-range values, which are possible with a float-based pipeline.
//
class OutputProducerTest : public testing::Test {
 public:
  std::unique_ptr<OutputProducer> SelectOutputProducer(
      fuchsia::media::AudioSampleFormat dest_format, int32_t num_channels) {
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
  void ShiftRightBy(std::vector<float>& floats, int32_t shift_by) {
    for (float& val : floats) {
      for (auto shift_num = 0; shift_num < shift_by; ++shift_num) {
        val *= 0.5f;
      }
    }
  }
};

// Create OutputProducer objects for outgoing buffers of type uint8
TEST_F(OutputProducerTest, ConstructionUint8) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2));
}

// Create OutputProducer objects for outgoing buffers of type int16
TEST_F(OutputProducerTest, ConstructionInt16) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 4));
}

// Create OutputProducer objects for outgoing buffers of type int24-in-32
TEST_F(OutputProducerTest, ConstructionInt24) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 3));
}

// Create OutputProducer objects for outgoing buffers of type float
TEST_F(OutputProducerTest, ConstructionFloat32) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1));
}

// Are all valid data values rounded correctly to 8-bit outputs?
TEST_F(OutputProducerTest, PassThruUint8) {
  // Destination buffer to be overwritten.
  auto dest = std::array<uint8_t, 8>{12, 23, 34, 45, 56, 67, 78, 89};

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
  ASSERT_EQ(dest.size(), accum.size());

  // The "becomes" values mentioned above.
  auto expect = std::array<uint8_t, 8>{0x00, 0x00, 0x3F, 0x80, 0x80, 0xC1, 0xFF, 0xFF};
  static_assert(std::size(dest) == std::size(expect));

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size());
  EXPECT_THAT(dest, testing::Pointwise(testing::Eq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs?
TEST_F(OutputProducerTest, PassThruInt16) {
  // Destination buffer to be overwritten.
  auto dest = std::array<int16_t, 8>{0123, 1234, 2345, 3456, 4567, 5678, 6789, 7890};

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
  ShiftRightBy(accum, 23);  // shift by six hex digits (minus the sign bit, as always)
  ASSERT_EQ(dest.size(), accum.size());

  // The "becomes" values mentioned below.
  auto expect = std::array<int16_t, 8>{-0x8000, -0x8000, -0x4081, -1, 0, 0x4081, 0x7FFF, 0x7FFF};
  static_assert(std::size(dest) == std::size(expect));

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size());
  EXPECT_THAT(dest, testing::Pointwise(testing::Eq(), expect));
}

// Are all valid data values passed correctly to 24-bit outputs?
// int24-in-32 has 1 fewer bit than float32 so we add a least-significant hex digit to specify
// values to be rounded. The additional bit (the final 0x08 below) is the equivalent of .5 or 0
TEST_F(OutputProducerTest, PassThruInt24) {
  // Destination buffer to be overwritten.
  auto dest = std::array<int32_t, 12>{0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                      0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};

  auto accum = std::vector<float>{
      // clang-format off
      -0x8000010, // clamped to the int24-in-32 min -0x80000000
      -0x8000000, // becomes -0x80000000, the int24-in-32 min
      -0x6543210, // becomes -0x65432100
      -0x4080808, // becomes -0x40808100, we round -0x0.8 out (down)
      -0x0234567, // becomes -0x02345600, we round -0x0.7 in  (up)
               0, // becomes  0x00000000
       0x0123450, // becomes  0x01234500
       0x0234567, // becomes  0x02345600, we round 0x0.7 in  (down)
       0x4080808, // becomes  0x40808100, we round 0x0.8 out (up)
       0x6543210, // becomes  0x65432100
       0x7FFFFF0, // becomes  0x7FFFFF00, the int24-in-32 max
       0x8000000, // clamped to the int24-in-32 max 0x7FFFFF00
      // clang-format on
  };
  ShiftRightBy(accum, 27);  // shift by seven hex digits (minus the sign bit, as always)
  ASSERT_EQ(dest.size(), accum.size());

  // The "becomes" values mentioned above.
  auto expect = std::array<int32_t, 12>{
      // clang-format off
      media_audio::kMinInt24In32,
      media_audio::kMinInt24In32,
        -0x65432100,
        -0x40808100,
        -0x02345600,
                  0,
         0x01234500,
         0x02345600,
         0x40808100,
         0x65432100,
      media_audio::kMaxInt24In32,
      media_audio::kMaxInt24In32,
      // clang-format on
  };
  static_assert(std::size(dest) == std::size(expect));

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size());
  EXPECT_THAT(dest, testing::Pointwise(testing::Eq(), expect));
}

// Are all valid data values passed correctly to float outputs
TEST_F(OutputProducerTest, PassThruFloat32) {
  auto accum = std::array<float, 9>{
      -1.1f, 1.1f, -1.0f, 1.0f, -0.503921568f, 0.503921568f, -0.000000119f, 0.000000119f, 0,
  };

  constexpr float kFillValue = 4.2f;
  auto dest = std::vector<float>(accum.size());
  std::fill(dest.begin(), dest.end(), kFillValue);
  ASSERT_EQ(dest.size(), accum.size());

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size());
  // Update the places where accum should differ from dest, so we can compare entire arrays.
  accum[0] = -1.0f;  // value was clamped
  accum[1] = 1.0f;   // value was clamped
  EXPECT_THAT(accum, testing::Pointwise(testing::FloatEq(), dest));
}

// As above, but use 4 channels.
TEST_F(OutputProducerTest, PassThruFloat32With2Chan) {
  constexpr auto num_channels = 2;
  // For each frame, use different values per channel to verify channel independence.
  auto accum = std::array<float, 7 * num_channels>{
      // clang-format off
      -1.1f,          1.1f,
       1.1f,         -1.1f,
       1.0f,          1.0f,
       0.000000119f,  0,
          0,         -0.000000119f,
      -0.503921568f,  0.503921568f,
       0.0f,          0.0f,
      // clang-format on
  };

  constexpr float kFillValue = 4.2f;
  auto dest = std::vector<float>(accum.size());
  std::fill(dest.begin(), dest.end(), kFillValue);
  ASSERT_EQ(dest.size(), accum.size()) << "Test error, vector lengths should match";

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, num_channels);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size() / num_channels);
  // Update the places where accum should differ from dest, so we can compare entire arrays.
  accum[0] = -1.0f;  // value was clamped
  accum[1] = 1.0f;   // value was clamped
  accum[2] = 1.0f;   // value was clamped
  accum[3] = -1.0f;  // value was clamped
  EXPECT_THAT(accum, testing::Pointwise(testing::FloatEq(), dest));
}

// As above, but use 4 channels.
TEST_F(OutputProducerTest, PassThruFloat32With4Chan) {
  constexpr auto num_channels = 4;
  // For each frame, use different values per channel to verify channel independence.
  auto accum = std::array<float, 9 * num_channels>{
      // clang-format off
      -1.1f,   -1.0f,    1.0f,    1.1f,
      -0.75f,  -0.25f,   0.25f,   0.75f
      // clang-format on
  };

  constexpr float kFillValue = 4.2f;
  auto dest = std::vector<float>(accum.size());
  std::fill(dest.begin(), dest.end(), kFillValue);
  ASSERT_EQ(dest.size(), accum.size()) << "Test error, vector lengths should match";

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, num_channels);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size() / num_channels);
  // Update the places where accum should differ from dest, so we can compare entire arrays.
  accum[0] = -1.0f;  // value was clamped
  accum[3] = 1.0f;   // value was clamped
  EXPECT_THAT(accum, testing::Pointwise(testing::FloatEq(), dest));
}

// As above, but ensure that extra dest frames are not overwritten.
TEST_F(OutputProducerTest, PassThruFloat32DontOverwriteExtraDestFrames) {
  auto accum = std::array<float, 6>{
      -1.1f, 1.1f, -1.0f, 1.0f, NAN, NAN,
  };

  constexpr float kFillValue = 4.2f;
  auto dest = std::vector<float>(accum.size());
  std::fill(dest.begin(), dest.end(), kFillValue);
  ASSERT_EQ(dest.size(), accum.size());

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->ProduceOutput(accum.data(), dest.data(), accum.size() - 2);
  // Update the places where accum should differ from dest, so we can compare entire arrays.
  accum[0] = -1.0f;                      // value was clamped
  accum[1] = 1.0f;                       // value was clamped
  accum[accum.size() - 2] = kFillValue;  // previous not-overwritten dest value
  accum[accum.size() - 1] = kFillValue;  // previous not-overwritten dest value
  EXPECT_THAT(accum, testing::Pointwise(testing::FloatEq(), dest));
}

// Are 8-bit output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, SilenceUint8) {
  constexpr auto num_frames = 6;
  std::array<uint8_t, num_frames> dest;
  std::fill(dest.begin(), dest.end(), 0xFF);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1);
  ASSERT_NE(nullptr, output_producer);

  output_producer->FillWithSilence(dest.data(), num_frames);
  EXPECT_THAT(dest, testing::Each(testing::Eq(0x80)));
}

// Are 16-bit output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, SilenceInt16) {
  constexpr auto num_frames = 6;
  std::array<int16_t, num_frames> dest;
  std::fill(dest.begin(), dest.end(), 9876);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_frames);
  EXPECT_THAT(dest, testing::Each(testing::Eq(0)));
}

// Are 24-bit output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, SilenceInt24) {
  constexpr auto num_frames = 6;
  std::array<int32_t, num_frames> dest;
  std::fill(dest.begin(), dest.end(), 0x12345678);

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_frames);
  EXPECT_THAT(dest, testing::Each(testing::Eq(0)));
}

// Are float output buffers correctly silenced? Do we stop when we should?
TEST_F(OutputProducerTest, SilenceFloat32) {
  constexpr auto num_frames = 6;
  std::array<float, num_frames> dest;
  std::fill(dest.begin(), dest.end(), -4.2f);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_frames);
  EXPECT_THAT(dest, testing::Each(testing::Eq(0.0f)));
}

// As above, but use multiple channels
TEST_F(OutputProducerTest, SilenceFloat32MultipleChannels) {
  constexpr auto num_channels = 3;
  constexpr auto num_frames = 4;
  std::array<float, num_channels * num_frames> dest;
  std::fill(dest.begin(), dest.end(), -4.2f);

  auto output_producer =
      SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, num_channels);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_frames);
  EXPECT_THAT(dest, testing::Each(testing::Eq(0.0f)));
}

// As above, but ensure that extra dest frames are not overwritten.
TEST_F(OutputProducerTest, SilenceFloat32DontOverwriteExtraDestFrames) {
  constexpr auto num_frames = 6;
  std::array<float, num_frames> dest;
  std::fill(dest.begin(), dest.end(), -4.2f);

  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(output_producer, nullptr);

  output_producer->FillWithSilence(dest.data(), num_frames - 2);
  // Expect not overwritten.
  EXPECT_EQ(dest[num_frames - 2], -4.2f);
  EXPECT_EQ(dest[num_frames - 1], -4.2f);
  // Overwrite so we can test the full array.
  dest[num_frames - 2] = 0.0f;
  dest[num_frames - 1] = 0.0f;
  EXPECT_THAT(dest, testing::Each(testing::Eq(0.0f)));
}

// Mixer objects produce normal data, but arbitrary pipeline effects may not.
//
// Currently OutputProducer clamps +/-INF to [-1.0, 1.0].
TEST_F(OutputProducerTest, InfinitiesFloat32) {
  float source, output;
  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(nullptr, output_producer);

  source = -INFINITY;  // will be clamped
  output_producer->ProduceOutput(&source, &output, 1);
  EXPECT_FLOAT_EQ(output, -1);
  EXPECT_TRUE(std::isnormal(output));

  source = INFINITY;  // will be clamped
  output_producer->ProduceOutput(&source, &output, 1);
  EXPECT_FLOAT_EQ(output, 1);
  EXPECT_TRUE(std::isnormal(output));
}

// Currently OutputProducer makes no explicit effort to detect and prevent NAN output.
// TODO(fxbug.dev/84260): Consider a mode where we eliminate NANs (presumably emitting 0 instead).
TEST_F(OutputProducerTest, DISABLED_NanFloat32) {
  float source, output;
  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(nullptr, output_producer);

  source = NAN;  // should be changed to zero
  output_producer->ProduceOutput(&source, &output, 1);
  EXPECT_FALSE(isnan(output));
  EXPECT_FLOAT_EQ(output, 0.0f);
}

// Currently OutputProducer makes no explicit effort to detect and prevent subnormal output.
// TODO(fxbug.dev/84260): Consider a mode where we detect subnormals and round to zero.
TEST_F(OutputProducerTest, DISABLED_SubnormalsFloat32) {
  float source, output;
  auto output_producer = SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1);
  ASSERT_NE(nullptr, output_producer);

  source = -FLT_MIN / 2.0;  // subnormal; should be rounded to zero
  output_producer->ProduceOutput(&source, &output, 1);
  EXPECT_FLOAT_EQ(output, 0.0f);

  source = FLT_MIN / 2.0;  // subnormal; should be rounded to zero
  output_producer->ProduceOutput(&source, &output, 1);
  EXPECT_FLOAT_EQ(output, 0.0f);
}

}  // namespace
}  // namespace media::audio
