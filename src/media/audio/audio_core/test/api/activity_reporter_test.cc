// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/tuning/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class ActivityReporterTest : public HermeticAudioTest {
 protected:
  void TearDown() override {
    audio_renderer_.Unbind();
    audio_capturer_.Unbind();

    HermeticAudioTest::TearDown();
  }

  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
};

// Test that the user is connected to the activity reporter.
// TODO(50645): More complete testing of the integration with renderers
TEST_F(ActivityReporterTest, ConnectToActivityReporter) {
  fuchsia::media::ActivityReporterPtr activity_reporter;
  environment()->ConnectToService(activity_reporter.NewRequest());
  AddErrorHandler(activity_reporter, "ActivityReporter");

  activity_reporter->WatchRenderActivity(AddCallback("WatchRenderActivity"));
  ExpectCallback();
}

}  // namespace media::audio::test
