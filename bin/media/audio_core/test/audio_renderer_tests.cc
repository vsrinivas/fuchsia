// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>

#include "lib/component/cpp/environment_services.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

// Base class for tests of the asynchronous AudioRenderer interface.
class AudioRendererTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "Audio connection lost. Quitting.";
      error_occurred_ = true;
      QuitLoop();
    });

    audio_->CreateRendererV2(audio_renderer_.NewRequest());
    ASSERT_TRUE(audio_renderer_);

    audio_renderer_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "AudioRenderer connection lost. Quitting.";
      error_occurred_ = true;
      QuitLoop();
    });
  }
  void TearDown() override { EXPECT_FALSE(error_occurred_); }

  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRenderer2Ptr audio_renderer_;
  bool error_occurred_ = false;
};

// Basic validation of SetPcmStreamType() for the asynchronous AudioRenderer.
TEST_F(AudioRendererTest, SetPcmStreamType) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_renderer_->SetPcmStreamType(std::move(format));

  int64_t lead_time = -1;
  audio_renderer_->GetMinLeadTime([this, &lead_time](int64_t min_lead_time) {
    lead_time = min_lead_time;
    QuitLoop();
  });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_GE(lead_time, 0);
}

// If renderer is not in operational mode, a second SetPcmStreamType should
// succeed.
TEST_F(AudioRendererTest, SetPcmFormat_Double) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_renderer_->SetPcmStreamType(std::move(format));

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format2.channels = 2;
  format2.frames_per_second = 44100;
  audio_renderer_->SetPcmStreamType(std::move(format2));

  int64_t lead_time = -1;
  audio_renderer_->GetMinLeadTime([this, &lead_time](int64_t min_lead_time) {
    lead_time = min_lead_time;
    QuitLoop();
  });

  EXPECT_FALSE(RunLoopWithTimeout(zx::msec(100)));
  EXPECT_GE(lead_time, 0);
}

// Base class for tests of the synchronous AudioRendererSync interface.
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
//
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
class AudioRendererSyncTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    ASSERT_EQ(ZX_OK, audio_->CreateRendererV2(audio_renderer_.NewRequest()));
    ASSERT_TRUE(audio_renderer_);
  }

  fuchsia::media::AudioSyncPtr audio_;
  fuchsia::media::AudioRenderer2SyncPtr audio_renderer_;
};

// Basic validation of SetPcmStreamType() for the synchronous AudioRenderer.
TEST_F(AudioRendererSyncTest, SetPcmStreamType) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_EQ(ZX_OK, audio_renderer_->SetPcmStreamType(std::move(format)));

  int64_t min_lead_time = -1;
  ASSERT_EQ(ZX_OK, audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

// If renderer is not in operational mode, a second SetPcmStreamType should
// succeed.
TEST_F(AudioRendererSyncTest, SetPcmFormat_Double) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_EQ(ZX_OK, audio_renderer_->SetPcmStreamType(std::move(format)));

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  EXPECT_EQ(ZX_OK, audio_renderer_->SetPcmStreamType(std::move(format2)));

  int64_t min_lead_time = -1;
  EXPECT_EQ(ZX_OK, audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

}  // namespace test
}  // namespace audio
}  // namespace media
