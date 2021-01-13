// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda/utils/utils.h"

#include <lib/sync/completion.h>
#include <zircon/errors.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

namespace audio::intel_hda {
namespace {

class AutoAdvancingClock : public Clock {
 public:
  zx::time Now() { return now_; }
  void SleepUntil(zx::time time) { now_ = std::max(now_, time); }
  void AdvanceTime(zx::duration duration) { now_ = now_ + duration; }

 private:
  zx::time now_{};
};

TEST(WaitCondition, AlwaysTrue) {
  EXPECT_OK(WaitCondition(ZX_USEC(0), ZX_USEC(0), []() { return true; }));
}

TEST(WaitCondition, AlwaysFalse) {
  EXPECT_EQ(WaitCondition(ZX_USEC(0), ZX_USEC(0), []() { return false; }), ZX_ERR_TIMED_OUT);
}

TEST(WaitCondition, FrequentPolling) {
  AutoAdvancingClock clock{};
  int num_polls = 0;

  // Poll every second for 10 seconds.
  WaitCondition(
      ZX_SEC(10), ZX_SEC(1),
      [&num_polls]() {
        num_polls++;
        return false;
      },
      &clock);

  // Ensure we polled 10 + 1 times.
  EXPECT_EQ(num_polls, 11);
}

TEST(WaitCondition, LongPollPeriod) {
  AutoAdvancingClock clock{};
  int num_polls = 0;

  // Have a polling period far greater than the deadline.
  WaitCondition(
      ZX_SEC(1), ZX_SEC(100),
      [&num_polls]() {
        num_polls++;
        return false;
      },
      &clock);

  // Ensure we poll twice, once at the beginning and once at the end.
  EXPECT_EQ(num_polls, 2);
  EXPECT_EQ(clock.Now(), zx::time(0) + zx::sec(1));
}

TEST(WaitCondition, LongRunningCondition) {
  AutoAdvancingClock clock{};
  int num_polls = 0;

  // We want to poll every second for 10 seconds, but the condition takes
  // 100 seconds to evaluate.
  WaitCondition(
      ZX_SEC(10), ZX_SEC(1),
      [&num_polls, &clock]() {
        num_polls++;
        clock.AdvanceTime(zx::sec(100));
        return false;
      },
      &clock);

  // Ensure that we still polled twice.
  EXPECT_EQ(num_polls, 2);
}

TEST(SampleCapabilities, MakeNewFromShortAudioDescriptorNoMatchFound) {
  SampleCaps old_sample_caps = {};
  old_sample_caps.pcm_size_rate_ = IHDA_PCM_SIZE_16BITS | IHDA_PCM_RATE_11025 |
                                   IHDA_PCM_RATE_16000 | IHDA_PCM_RATE_22050 | IHDA_PCM_RATE_32000;
  old_sample_caps.pcm_formats_ = IHDA_PCM_FORMAT_PCM;
  SampleCaps new_sample_caps = {};
  edid::ShortAudioDescriptor sad_list[1];
  uint8_t num_channels_minus_1 = 1;
  sad_list[0].format_and_channels = (edid::ShortAudioDescriptor::kLPcm << 3) | num_channels_minus_1;
  sad_list[0].sampling_frequencies = edid::ShortAudioDescriptor::kHz48;
  sad_list[0].bitrate = edid::ShortAudioDescriptor::kLpcm_20 | edid::ShortAudioDescriptor::kLpcm_24;
  ASSERT_EQ(MakeNewSampleCaps(old_sample_caps, sad_list, countof(sad_list), new_sample_caps),
            ZX_ERR_NOT_FOUND);
  EXPECT_EQ(new_sample_caps.pcm_size_rate_, 0);
}

TEST(SampleCapabilities, MakeNewFromShortAudioDescriptorBadPcmFormat) {
  SampleCaps old_sample_caps = {};
  old_sample_caps.pcm_size_rate_ = IHDA_PCM_SIZE_16BITS | IHDA_PCM_RATE_48000;
  old_sample_caps.pcm_formats_ =
      IHDA_PCM_FORMAT_AC3 | IHDA_PCM_FORMAT_FLOAT32;  // No PCM, error below.
  SampleCaps new_sample_caps = {};
  edid::ShortAudioDescriptor sad_list[1];
  uint8_t num_channels_minus_1 = 1;
  sad_list[0].format_and_channels = (edid::ShortAudioDescriptor::kLPcm << 3) | num_channels_minus_1;
  sad_list[0].sampling_frequencies = edid::ShortAudioDescriptor::kHz48;
  sad_list[0].bitrate = edid::ShortAudioDescriptor::kLpcm_20 | edid::ShortAudioDescriptor::kLpcm_24;
  ASSERT_EQ(MakeNewSampleCaps(old_sample_caps, sad_list, countof(sad_list), new_sample_caps),
            ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(new_sample_caps.pcm_size_rate_, 0);
}

TEST(SampleCapabilities, MakeNewFromShortAudioDescriptorFindSingle) {
  SampleCaps old_sample_caps = {};
  old_sample_caps.pcm_size_rate_ = IHDA_PCM_SIZE_16BITS | IHDA_PCM_RATE_11025 |
                                   IHDA_PCM_RATE_16000 | IHDA_PCM_RATE_22050 | IHDA_PCM_RATE_32000;
  old_sample_caps.pcm_formats_ = IHDA_PCM_FORMAT_PCM;
  SampleCaps new_sample_caps = {};
  edid::ShortAudioDescriptor sad_list[1];
  sad_list[0].format_and_channels = 0x09;  // format = 1, num channels minus 1 = 1.
  sad_list[0].sampling_frequencies = edid::ShortAudioDescriptor::kHz32 |
                                     edid::ShortAudioDescriptor::kHz44 |
                                     edid::ShortAudioDescriptor::kHz48;
  sad_list[0].bitrate = edid::ShortAudioDescriptor::kLpcm_16 |
                        edid::ShortAudioDescriptor::kLpcm_20 | edid::ShortAudioDescriptor::kLpcm_24;
  ASSERT_OK(MakeNewSampleCaps(old_sample_caps, sad_list, countof(sad_list), new_sample_caps));
  EXPECT_EQ(new_sample_caps.pcm_size_rate_, IHDA_PCM_SIZE_16BITS | IHDA_PCM_RATE_32000);
}

TEST(SampleCapabilities, MakeNewFromShortAudioDescriptorFindMultiple1) {
  SampleCaps old_sample_caps = {};
  old_sample_caps.pcm_size_rate_ = IHDA_PCM_SIZE_16BITS | IHDA_PCM_SIZE_20BITS |
                                   IHDA_PCM_SIZE_24BITS | IHDA_PCM_RATE_32000 | IHDA_PCM_RATE_48000;
  old_sample_caps.pcm_formats_ = IHDA_PCM_FORMAT_PCM;
  SampleCaps new_sample_caps = {};
  edid::ShortAudioDescriptor sad_list[1];
  sad_list[0].format_and_channels = 0x09;  // format = 1, num channels minus 1 = 1.
  sad_list[0].sampling_frequencies = edid::ShortAudioDescriptor::kHz32 |
                                     edid::ShortAudioDescriptor::kHz44 |
                                     edid::ShortAudioDescriptor::kHz48;
  sad_list[0].bitrate = edid::ShortAudioDescriptor::kLpcm_20;
  ASSERT_OK(MakeNewSampleCaps(old_sample_caps, sad_list, countof(sad_list), new_sample_caps));
  EXPECT_EQ(new_sample_caps.pcm_size_rate_,
            IHDA_PCM_SIZE_20BITS | IHDA_PCM_RATE_32000 | IHDA_PCM_RATE_48000);
}

TEST(SampleCapabilities, MakeNewFromShortAudioDescriptorFindMultiple2) {
  SampleCaps old_sample_caps = {};
  old_sample_caps.pcm_size_rate_ = IHDA_PCM_SIZE_16BITS | IHDA_PCM_SIZE_20BITS |
                                   IHDA_PCM_SIZE_24BITS | IHDA_PCM_RATE_32000 | IHDA_PCM_RATE_48000;
  old_sample_caps.pcm_formats_ = IHDA_PCM_FORMAT_PCM;
  SampleCaps new_sample_caps = {};
  edid::ShortAudioDescriptor sad_list[1];
  sad_list[0].format_and_channels = 0x09;  // format = 1, num channels minus 1 = 1.
  sad_list[0].sampling_frequencies = edid::ShortAudioDescriptor::kHz48;
  sad_list[0].bitrate = edid::ShortAudioDescriptor::kLpcm_20 | edid::ShortAudioDescriptor::kLpcm_24;
  ASSERT_OK(MakeNewSampleCaps(old_sample_caps, sad_list, countof(sad_list), new_sample_caps));
  EXPECT_EQ(new_sample_caps.pcm_size_rate_,
            IHDA_PCM_SIZE_20BITS | IHDA_PCM_SIZE_24BITS | IHDA_PCM_RATE_48000);
}
}  // namespace
}  // namespace audio::intel_hda
