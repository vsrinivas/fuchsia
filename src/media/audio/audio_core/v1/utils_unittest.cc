// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/utils.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/errors.h>

#include <cstdint>
#include <unordered_map>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/v1/testing/fake_profile_provider.h"

namespace media::audio {
namespace {

void AddChannelSet(fuchsia::hardware::audio::PcmSupportedFormats& formats,
                   size_t number_of_channels) {
  fuchsia::hardware::audio::ChannelSet channel_set = {};
  std::vector<fuchsia::hardware::audio::ChannelAttributes> attributes(number_of_channels);
  channel_set.set_attributes(std::move(attributes));
  formats.mutable_channel_sets()->push_back(std::move(channel_set));
}

class UtilsTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    auto svc = context_provider_.service_directory_provider();
    ASSERT_EQ(ZX_OK, svc->AddService(profile_provider_.GetHandler()));
  }

  FakeProfileProvider* profile_provider() { return &profile_provider_; }

  sys::ComponentContext* context() { return context_provider_.context(); }

 private:
  FakeProfileProvider profile_provider_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(UtilsTest, AcquireAudioCoreImplProfile) {
  ASSERT_TRUE(profile_provider()->SetProfile(24));
  zx_status_t status = ZX_ERR_NOT_FOUND;

  AcquireAudioCoreImplProfile(context(), [&status](zx_status_t s, zx::profile p) { status = s; });
  RunLoopUntilIdle();

  ASSERT_EQ(status, ZX_OK);
}

TEST_F(UtilsTest, AcquireAudioCoreImplProfile_ProfileUnavailable) {
  bool callback_invoked = false;
  zx_status_t status = ZX_ERR_NOT_FOUND;
  AcquireAudioCoreImplProfile(context(), [&](zx_status_t s, zx::profile p) {
    status = s;
    callback_invoked = true;
  });
  RunLoopUntilIdle();

  ASSERT_NE(status, ZX_OK);
  ASSERT_TRUE(callback_invoked);
}

TEST(UtilsFormatTest, SelectBestFormatFound) {
  std::vector<audio_stream_format_range_t> fmts;
  fmts.push_back(audio_stream_format_range_t{
      .sample_formats = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
      .min_frames_per_second = 12'000,
      .max_frames_per_second = 96'000,
      .min_channels = 1,
      .max_channels = 8,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  });
  fuchsia::media::AudioSampleFormat sample_format_inout = fuchsia::media::AudioSampleFormat::FLOAT;
  uint32_t frames_per_second_inout = 96'000;
  uint32_t channels_inout = 1;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(96'000));
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(1));

  // Add a second format range.
  fmts.push_back(audio_stream_format_range_t{
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = 22'050,
      .max_frames_per_second = 176'400,
      .min_channels = 4,
      .max_channels = 8,
      .flags = ASF_RANGE_FLAG_FPS_44100_FAMILY,
  });
  sample_format_inout = fuchsia::media::AudioSampleFormat::SIGNED_16;
  frames_per_second_inout = 88'200;
  channels_inout = 5;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(88'200));
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(5));
}

TEST(UtilsFormatTest, SelectBestFormatFoundFidl) {
  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> fmts;

  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  AddChannelSet(formats, 1);
  AddChannelSet(formats, 2);
  AddChannelSet(formats, 4);
  AddChannelSet(formats, 8);
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_FLOAT);
  formats.mutable_bytes_per_sample()->push_back(4);
  formats.mutable_valid_bits_per_sample()->push_back(32);
  formats.mutable_frame_rates()->push_back(12'000);
  formats.mutable_frame_rates()->push_back(24'000);
  formats.mutable_frame_rates()->push_back(48'000);
  formats.mutable_frame_rates()->push_back(96'000);
  fmts.push_back(std::move(formats));

  fuchsia::media::AudioSampleFormat sample_format_inout = fuchsia::media::AudioSampleFormat::FLOAT;
  uint32_t frames_per_second_inout = 96'000;
  uint32_t channels_inout = 1;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(96'000));
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(1));

  // Add a second format range.
  fuchsia::hardware::audio::PcmSupportedFormats formats2 = {};
  AddChannelSet(formats2, 4);
  AddChannelSet(formats2, 5);
  AddChannelSet(formats2, 6);
  AddChannelSet(formats2, 7);
  AddChannelSet(formats2, 8);
  formats2.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats2.mutable_bytes_per_sample()->push_back(2);
  formats2.mutable_valid_bits_per_sample()->push_back(16);
  formats2.mutable_frame_rates()->push_back(22'050);
  formats2.mutable_frame_rates()->push_back(44'100);
  formats2.mutable_frame_rates()->push_back(88'200);
  formats2.mutable_frame_rates()->push_back(176'400);
  fmts.push_back(std::move(formats2));

  sample_format_inout = fuchsia::media::AudioSampleFormat::SIGNED_16;
  frames_per_second_inout = 88'200;
  channels_inout = 5;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(88'200));
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(5));
}

TEST(UtilsFormatTest, SelectBestFormatOutsideRanges) {
  std::vector<audio_stream_format_range_t> fmts;
  fmts.push_back(audio_stream_format_range_t{
      .sample_formats = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
      .min_frames_per_second = 16'000,
      .max_frames_per_second = 96'000,
      .min_channels = 1,
      .max_channels = 8,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  });
  fuchsia::media::AudioSampleFormat sample_format_inout =
      fuchsia::media::AudioSampleFormat::SIGNED_16;
  uint32_t frames_per_second_inout = 0;
  uint32_t channels_inout = 0;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(16'000));  // Prefer closest.
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(2));                // Prefer 2 channels.

  sample_format_inout = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  frames_per_second_inout = 192'000;
  channels_inout = 200;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(96'000));  // Pick closest.
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(2));                // Prefer 2 channels.

  // Add a second format range.
  fmts.push_back(audio_stream_format_range_t{
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = 16'000,
      .max_frames_per_second = 24'000,
      .min_channels = 4,
      .max_channels = 8,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  });
  frames_per_second_inout = 0;
  channels_inout = 0;
  sample_format_inout = fuchsia::media::AudioSampleFormat::UNSIGNED_8;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::SIGNED_16);  // Pick 16 bits.
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(16'000));             // Pick closest.
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(8));                           // Pick highest.
}

TEST(UtilsFormatTest, SelectBestFormatOutsideRangesFidl) {
  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> fmts;
  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  AddChannelSet(formats, 1);
  AddChannelSet(formats, 2);
  AddChannelSet(formats, 4);
  AddChannelSet(formats, 8);
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_FLOAT);
  formats.mutable_bytes_per_sample()->push_back(4);
  formats.mutable_valid_bits_per_sample()->push_back(32);
  formats.mutable_frame_rates()->push_back(16'000);
  formats.mutable_frame_rates()->push_back(24'000);
  formats.mutable_frame_rates()->push_back(48'000);
  formats.mutable_frame_rates()->push_back(96'000);
  fmts.push_back(std::move(formats));

  fuchsia::media::AudioSampleFormat sample_format_inout =
      fuchsia::media::AudioSampleFormat::SIGNED_16;
  uint32_t frames_per_second_inout = 0;
  uint32_t channels_inout = 0;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(16'000));  // Prefer closest available.
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(2));                // Prefer 2 channels.

  sample_format_inout = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  frames_per_second_inout = 192'000;
  channels_inout = 200;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(96'000));  // Pick closest available.
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(2));                // Prefer 2 channels.

  // Add a second format range.
  fuchsia::hardware::audio::PcmSupportedFormats formats2 = {};
  AddChannelSet(formats2, 4);
  AddChannelSet(formats2, 5);
  AddChannelSet(formats2, 6);
  AddChannelSet(formats2, 7);
  AddChannelSet(formats2, 8);
  formats2.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats2.mutable_bytes_per_sample()->push_back(2);
  formats2.mutable_valid_bits_per_sample()->push_back(16);
  formats2.mutable_frame_rates()->push_back(16'000);
  formats2.mutable_frame_rates()->push_back(24'000);
  fmts.push_back(std::move(formats2));

  frames_per_second_inout = 0;
  channels_inout = 0;
  sample_format_inout = fuchsia::media::AudioSampleFormat::UNSIGNED_8;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_OK);
  ASSERT_EQ(sample_format_inout, fuchsia::media::AudioSampleFormat::SIGNED_16);  // Pick 16 bits.
  ASSERT_EQ(frames_per_second_inout, static_cast<uint32_t>(16'000));             // Pick closest.
  ASSERT_EQ(channels_inout, static_cast<uint32_t>(8));                           // Pick highest.
}

TEST(UtilsFormatTest, SelectBestFormatError) {
  std::vector<audio_stream_format_range_t> fmts;
  fmts.push_back(audio_stream_format_range_t{
      .sample_formats = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
      .min_frames_per_second = 8'000,
      .max_frames_per_second = 768'000,
      .min_channels = 1,
      .max_channels = 8,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  });
  fuchsia::media::AudioSampleFormat sample_format_inout = {};  // Bad format.
  uint32_t frames_per_second_inout = 0;
  uint32_t channels_inout = 0;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_ERR_INVALID_ARGS);

  sample_format_inout = fuchsia::media::AudioSampleFormat::SIGNED_16;  // Fix format.

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, nullptr,  // Bad pointer.
                             &sample_format_inout),
            ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, nullptr),
            ZX_ERR_INVALID_ARGS);  // Bad pointer.

  std::vector<audio_stream_format_range_t> empty_fmts;
  ASSERT_EQ(
      SelectBestFormat(empty_fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
      ZX_ERR_NOT_SUPPORTED);
}

TEST(UtilsFormatTest, SelectBestFormatErrorFidl) {
  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> fmts;

  fuchsia::hardware::audio::PcmSupportedFormats formats = {};
  AddChannelSet(formats, 1);
  AddChannelSet(formats, 2);
  AddChannelSet(formats, 4);
  AddChannelSet(formats, 8);
  formats.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_FLOAT);
  formats.mutable_bytes_per_sample()->push_back(1);
  formats.mutable_valid_bits_per_sample()->push_back(32);
  formats.mutable_frame_rates()->push_back(8'000);
  formats.mutable_frame_rates()->push_back(16'000);
  formats.mutable_frame_rates()->push_back(24'000);
  formats.mutable_frame_rates()->push_back(48'000);
  formats.mutable_frame_rates()->push_back(96'000);
  formats.mutable_frame_rates()->push_back(192'000);
  formats.mutable_frame_rates()->push_back(384'000);
  formats.mutable_frame_rates()->push_back(768'000);
  fmts.push_back(std::move(formats));

  fuchsia::media::AudioSampleFormat sample_format_inout = {};  // Bad format
  uint32_t frames_per_second_inout = 0;
  uint32_t channels_inout = 0;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
            ZX_ERR_INVALID_ARGS);

  sample_format_inout = fuchsia::media::AudioSampleFormat::SIGNED_16;

  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, nullptr,  // Bad pointer.
                             &sample_format_inout),
            ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(SelectBestFormat(fmts, &frames_per_second_inout, &channels_inout, nullptr),
            ZX_ERR_INVALID_ARGS);  // Bad pointer.

  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> empty_fmts;
  ASSERT_EQ(
      SelectBestFormat(empty_fmts, &frames_per_second_inout, &channels_inout, &sample_format_inout),
      ZX_ERR_NOT_SUPPORTED);
}

}  // namespace
}  // namespace media::audio
