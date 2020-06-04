// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/test/audio_buffer.h"

using testing::FloatEq;
using testing::Pointwise;

namespace media::audio::test {

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
class ComparatorsParameterizedTest : public testing::Test {};
using ComparatorsParamterTypes =
    ::testing::Types<ParamUnsigned8, ParamSigned16, ParamSigned24In32, ParamFloat>;
TYPED_TEST_SUITE(ComparatorsParameterizedTest, ComparatorsParamterTypes);

TYPED_TEST(ComparatorsParameterizedTest, CompareAudioBuffersSameSizeMatch) {
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

TYPED_TEST(ComparatorsParameterizedTest, CompareAudioBuffersSameSizeNotMatch) {
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

TYPED_TEST(ComparatorsParameterizedTest, CompareAudioBuffersGotLargerMatch) {
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

TYPED_TEST(ComparatorsParameterizedTest, CompareAudioBuffersPartialMatch) {
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

TYPED_TEST(ComparatorsParameterizedTest, CompareAudioBuffersPartialNotMatch) {
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

TEST(ComparatorsTest, CompareAudioBuffersFloatApproxMatch) {
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

TEST(ComparatorsTest, CompareAudioBuffersFloatApproxNotMatch) {
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
