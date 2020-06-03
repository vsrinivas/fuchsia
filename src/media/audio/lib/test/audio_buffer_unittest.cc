// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/audio_buffer.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format/format.h"

using testing::FloatEq;
using testing::Pointwise;

namespace media::audio::test {

TEST(AudioBufferTest, Basics) {
  constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 2,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<kSampleFormat> buf(format, 10);
  EXPECT_EQ(10U, buf.NumFrames());
  EXPECT_EQ(10U * 2 * sizeof(int16_t), buf.NumBytes());
  EXPECT_EQ(0U, buf.SampleIndex(0, 0));
  EXPECT_EQ(1U, buf.SampleIndex(0, 1));
  EXPECT_EQ(2U, buf.SampleIndex(1, 0));

  AudioBufferSlice slice1(&buf);
  EXPECT_EQ(10U, slice1.NumFrames());
  EXPECT_EQ(0U, slice1.SampleIndex(0, 0));
  EXPECT_EQ(3U, slice1.SampleIndex(1, 1));

  AudioBufferSlice slice2(&buf, 5, 8);
  EXPECT_EQ(3U, slice2.NumFrames());
  EXPECT_EQ(10U, slice2.SampleIndex(0, 0));
  EXPECT_EQ(13U, slice2.SampleIndex(1, 1));
}

TEST(AudioBufferTest, GenerateCosine_8) {
  const auto kSampleFormat = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();
  auto got = GenerateCosineAudio<kSampleFormat>(format, 2, 0.0, 0.0, 0.0);

  // Frequency 0.0 produces constant value. Val 0 is shifted to 0x80.
  EXPECT_EQ(got.samples, (std::vector<uint8_t>{0x80, 0x80}));
}

TEST(AudioBufferTest, GenerateCosine_16) {
  const auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  // Frequency of 0.0 produces constant value, with -.4 rounded toward zero.
  auto got = GenerateCosineAudio<kSampleFormat>(format, 2, 0, -32766.4);
  EXPECT_EQ(got.samples, (std::vector<int16_t>{-32766, -32766}));
}

TEST(AudioBufferTest, GenerateCosine_32) {
  const auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();
  auto got = GenerateCosineAudio<kSampleFormat>(format, 4, 1.0, 12345.6, M_PI);

  // PI phase leads to effective magnitude of -12345.6. At frequency 1.0, the change to the buffer
  // is [-12345.6, 0, +12345.6, 0], with +.6 values being rounded away from zero.
  EXPECT_EQ(got.samples, (std::vector<int32_t>{-12346, 0, 12346, 0}));
}

// Test float-based version of AccumCosine, including default amplitude (1.0)
TEST(AudioBufferTest, GenerateCosine_Float) {
  const auto kSampleFormat = fuchsia::media::AudioSampleFormat::FLOAT;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();
  auto got = GenerateCosineAudio<kSampleFormat>(format, 4, 0);
  EXPECT_THAT(got.samples, Pointwise(FloatEq(), (std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f})));

  // PI/2 shifts the freq:1 wave left by 1 here.
  got = GenerateCosineAudio<kSampleFormat>(format, 4, 1.0, 0.5, M_PI / 2.0);
  // cos(M_PI/2) is not exactly zero. Translate by 1 so that close-to-zero numbers are rounded to 1.
  for (size_t k = 0; k < 4; k++) {
    got.samples[k] += 1.0;
  }
  EXPECT_THAT(got.samples, Pointwise(FloatEq(), (std::vector<float>{1.0f, 0.5f, 1.0f, 1.5f})));
}

struct ParamUnsigned8 {
  static constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  using Traits = SampleFormatTraits<kSampleFormat>;
};

struct ParamSigned16 {
  static constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
  using Traits = SampleFormatTraits<kSampleFormat>;
};

struct ParamSigned24In32 {
  static constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
  using Traits = SampleFormatTraits<kSampleFormat>;
};

struct ParamFloat {
  static constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::FLOAT;
  using Traits = SampleFormatTraits<kSampleFormat>;
};

template <typename T>
class AudioBufferParameterizedTest : public testing::Test {};
using AudioBufferParamterTypes =
    ::testing::Types<ParamUnsigned8, ParamSigned16, ParamSigned24In32, ParamFloat>;
TYPED_TEST_SUITE(AudioBufferParameterizedTest, AudioBufferParamterTypes);

TYPED_TEST(AudioBufferParameterizedTest, CompareAudioBuffersSameSizeMatch) {
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = TypeParam::kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<TypeParam::kSampleFormat> got(format, 5);
  got.samples = {0, 1, 2, 3, 4};

  AudioBuffer<TypeParam::kSampleFormat> want(format, 5);
  want.samples = {0, 1, 2, 3, 4};

  CompareAudioBuffers<TypeParam::kSampleFormat>(&got, &want, {});
}

TYPED_TEST(AudioBufferParameterizedTest, CompareAudioBuffersSameSizeNotMatch) {
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = TypeParam::kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<TypeParam::kSampleFormat> got(format, 5);
  got.samples = {0, 1, 2, 3, 4};

  AudioBuffer<TypeParam::kSampleFormat> want(format, 5);
  want.samples = {0, 1, 9, 3, 4};

  EXPECT_NONFATAL_FAILURE(CompareAudioBuffers<TypeParam::kSampleFormat>(&got, &want, {}),
                          "unexpected value");
}

TYPED_TEST(AudioBufferParameterizedTest, CompareAudioBuffersGotLargerMatch) {
  constexpr auto kSilentValue = TypeParam::Traits::kSilentValue;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = TypeParam::kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<TypeParam::kSampleFormat> got(format, 8);
  got.samples = {0, 1, 2, 3, 4, kSilentValue, kSilentValue, kSilentValue};

  AudioBuffer<TypeParam::kSampleFormat> want(format, 5);
  want.samples = {0, 1, 2, 3, 4};

  CompareAudioBuffers<TypeParam::kSampleFormat>(&got, &want, {});
}

TYPED_TEST(AudioBufferParameterizedTest, CompareAudioBuffersPartialMatch) {
  constexpr auto kSilentValue = TypeParam::Traits::kSilentValue;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = TypeParam::kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<TypeParam::kSampleFormat> got(format, 5);
  got.samples = {0, 1, 2, kSilentValue, kSilentValue};

  AudioBuffer<TypeParam::kSampleFormat> want(format, 5);
  want.samples = {0, 1, 2, 3, 4};

  CompareAudioBuffers<TypeParam::kSampleFormat>(&got, &want, {.partial = true});
}

TYPED_TEST(AudioBufferParameterizedTest, CompareAudioBuffersPartialNotMatch) {
  constexpr auto kSilentValue = TypeParam::Traits::kSilentValue;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = TypeParam::kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<TypeParam::kSampleFormat> got(format, 5);
  got.samples = {0, 1, 2, kSilentValue, 4};

  AudioBuffer<TypeParam::kSampleFormat> want(format, 5);
  want.samples = {0, 1, 2, 3, 4};

  EXPECT_NONFATAL_FAILURE(
      CompareAudioBuffers<TypeParam::kSampleFormat>(&got, &want, {.partial = true}),
      "unexpected value");
}

TEST(AudioBufferTest, CompareAudioBuffersFloatApproxMatch) {
  constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::FLOAT;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  // Using the pythagorean quintuplet [1,1,3,5,6] so that RMS is a simple fraction.
  // sqrt(sum(want[k]^2)) = 0.6
  AudioBuffer<kSampleFormat> want(format, 4);
  want.samples = {0.1, 0.1, 0.3, 0.5};

  AudioBuffer<kSampleFormat> got(format, 4);
  got.samples = {0.1, 0.1, 0.29, 0.5};

  // diff = want-got = {0, 0, 0.01, 0}
  // RMS(diff) = 0.005
  // RMS(want) = 0.3
  // relative error = 0.01666...
  CompareAudioBuffers<kSampleFormat>(&got, &want, {.max_relative_error = 0.02});
}

TEST(AudioBufferTest, CompareAudioBuffersFloatApproxNotMatch) {
  constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::FLOAT;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 1,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  // Using the pythagorean quintuplet [1,1,3,5,6] so that RMS is a simple fraction.
  // sqrt(sum(want[k]^2)) = 0.6
  AudioBuffer<kSampleFormat> want(format, 4);
  want.samples = {0.1, 0.1, 0.3, 0.5};

  AudioBuffer<kSampleFormat> got(format, 4);
  got.samples = {0.1, 0.1, 0.29, 0.5};

  // diff = want-got = {0, 0, 0.01, 0}
  // RMS(diff) = 0.005
  // RMS(want) = 0.3
  // relative error = 0.01666...
  EXPECT_NONFATAL_FAILURE(
      CompareAudioBuffers<kSampleFormat>(&got, &want, {.max_relative_error = 0.01}),
      "relative error 0.01666");
}

}  // namespace media::audio::test
