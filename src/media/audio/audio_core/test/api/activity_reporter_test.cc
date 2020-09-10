// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <cmath>

#include <gmock/gmock.h>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

using AudioRenderUsage = fuchsia::media::AudioRenderUsage;
using AudioSampleFormat = fuchsia::media::AudioSampleFormat;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

namespace media::audio::test {

class ActivityReporterTest : public HermeticAudioTest {
 protected:
  void SetUp() override {
    HermeticAudioTest::SetUp();

    environment()->ConnectToService(activity_reporter_.NewRequest());
    AddErrorHandler(activity_reporter_, "ActivityReporter");
  }

  AudioRendererShim<AudioSampleFormat::SIGNED_16>* CreateAndPlayWithUsage(AudioRenderUsage usage) {
    auto format = Format::Create<AudioSampleFormat::SIGNED_16>(1, 8000).value();  // arbitrary
    auto r = CreateAudioRenderer(format, 1024, usage);
    r->fidl()->PlayNoReply(0, 0);
    return r;
  }

  fuchsia::media::ActivityReporterPtr activity_reporter_;
};

// Test that the user is connected to the activity reporter.
TEST_F(ActivityReporterTest, AddAndRemove) {
  std::vector<AudioRenderUsage> active_usages;
  auto add_callback = [this, &active_usages](std::string name) {
    active_usages.clear();
    activity_reporter_->WatchRenderActivity(AddCallback(
        name, [&active_usages](std::vector<AudioRenderUsage> u) { active_usages = u; }));
  };

  // First call should return immediately, others should wait for updates.
  add_callback("WatchRenderActivity InitialCall");
  ExpectCallback();
  EXPECT_EQ(active_usages, std::vector<AudioRenderUsage>{});

  add_callback("WatchRenderActivity AfterPlayBackground");
  auto r1 = CreateAndPlayWithUsage(AudioRenderUsage::BACKGROUND);
  ExpectCallback();
  EXPECT_THAT(active_usages, UnorderedElementsAreArray({AudioRenderUsage::BACKGROUND}));

  add_callback("WatchRenderActivity AfterPlayMedia");
  auto r2 = CreateAndPlayWithUsage(AudioRenderUsage::MEDIA);
  ExpectCallback();
  EXPECT_THAT(active_usages,
              UnorderedElementsAreArray({AudioRenderUsage::BACKGROUND, AudioRenderUsage::MEDIA}));

  add_callback("WatchRenderActivity AfterPauseBackground");
  r1->fidl()->PauseNoReply();
  ExpectCallback();
  EXPECT_THAT(active_usages, UnorderedElementsAreArray({AudioRenderUsage::MEDIA}));

  add_callback("WatchRenderActivity AfterDisconnectMedia");
  Unbind(r2);
  ExpectCallback();
  EXPECT_THAT(active_usages, UnorderedElementsAre());
}

TEST_F(ActivityReporterTest, DisconnectOnMultipleConcurrentCalls) {
  activity_reporter_->WatchRenderActivity(AddCallback("WatchRenderActivity"));
  ExpectCallback();

  activity_reporter_->WatchRenderActivity(AddUnexpectedCallback("WatchRenderActivity Unexpected1"));
  activity_reporter_->WatchRenderActivity(AddUnexpectedCallback("WatchRenderActivity Unexpected2"));
  ExpectDisconnect(activity_reporter_);
}

}  // namespace media::audio::test
