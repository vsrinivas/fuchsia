// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>

#include "lib/component/cpp/environment_services_helper.h"
#include "src/media/audio/audio_core/test/audio_tests_shared.h"

namespace media::audio::test {

//
// AudioRendererSyncTest
//
// Base class for tests of the synchronous AudioRendererSync interface.
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
//
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
class AudioRendererSyncTest : public gtest::RealLoopFixture {
 protected:
  // "Regional" per-test-suite set-up. Called before first test in this suite.
  static void SetUpTestSuite() {
    std::shared_ptr<component::Services> environment_services =
        component::GetEnvironmentServices();
    environment_services->ConnectToService(audio_sync_.NewRequest());
  }

  // Per-test-suite tear-down. Called after last test in this suite.
  static void TearDownTestSuite() { audio_sync_.Unbind(); }

  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    ASSERT_EQ(ZX_OK, audio_sync_->CreateAudioRenderer(
                         audio_renderer_sync_.NewRequest()));
  }

  void TearDown() override {
    audio_renderer_sync_.Unbind();
    ::gtest::RealLoopFixture::TearDown();
  }

  // Declare singleton resource shared by all test cases.
  static fuchsia::media::AudioSyncPtr audio_sync_;

  // One of these is created anew, for each test case.
  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync_;
};

fuchsia::media::AudioSyncPtr AudioRendererSyncTest::audio_sync_ = nullptr;

//
// AudioRendererSync validation
//
// Basic validation of GetMinLeadTime() for the synchronous AudioRenderer.
// In subsequent synchronous-interface test(s), receiving a valid return value
// from this call is our only way of verifying that the connection survived.
TEST_F(AudioRendererSyncTest, GetMinLeadTime) {
  int64_t min_lead_time = -1;
  ASSERT_EQ(ZX_OK, audio_renderer_sync_->GetMinLeadTime(&min_lead_time))
      << kConnectionErr;
  EXPECT_GE(min_lead_time, 0) << "No MinLeadTime update received";
}

// GetMinLeadTime(nullptr) results in the synchronous proxy terminating the
// client process, with no service-side impact -- no reason to test that here.

//
// Before renderers are operational, multiple SetPcmStreamTypes should succeed.
// We test twice because of previous bug, where the first succeeded but any
// subsequent call (before Play) would cause a FIDL channel disconnect.
// GetMinLeadTime is our way of verifying whether the connection survived.
TEST_F(AudioRendererSyncTest, SetPcmFormat) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_EQ(ZX_OK, audio_renderer_sync_->SetPcmStreamType(format));

  int64_t min_lead_time = -1;
  ASSERT_EQ(ZX_OK, audio_renderer_sync_->GetMinLeadTime(&min_lead_time))
      << kConnectionErr;
  EXPECT_GE(min_lead_time, 0);

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  EXPECT_EQ(ZX_OK, audio_renderer_sync_->SetPcmStreamType(format2));

  min_lead_time = -1;
  EXPECT_EQ(ZX_OK, audio_renderer_sync_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

// Before setting format, PlayNoReply should cause a Disconnect.
// GetMinLeadTime is our way of verifying whether the connection survived.
TEST_F(AudioRendererSyncTest, PlayNoReplyNoFormatCausesDisconnect) {
  int64_t min_lead_time = -1;
  // First, make sure we still have a renderer at all.
  ASSERT_EQ(ZX_OK, audio_renderer_sync_->GetMinLeadTime(&min_lead_time));

  EXPECT_EQ(ZX_OK,
            audio_renderer_sync_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                                              fuchsia::media::NO_TIMESTAMP));

  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            audio_renderer_sync_->GetMinLeadTime(&min_lead_time));
  // Although the connection has disconnected, the proxy should still exist.
  EXPECT_TRUE(audio_renderer_sync_.is_bound());
}

// Before setting format, PauseNoReply should cause a Disconnect.
// GetMinLeadTime is our way of verifying whether the connection survived.
TEST_F(AudioRendererSyncTest, PauseNoReplyWithoutFormatCausesDisconnect) {
  int64_t min_lead_time = -1;
  // First, make sure we still have a renderer at all.
  ASSERT_EQ(ZX_OK, audio_renderer_sync_->GetMinLeadTime(&min_lead_time));

  EXPECT_EQ(ZX_OK, audio_renderer_sync_->PauseNoReply());

  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            audio_renderer_sync_->GetMinLeadTime(&min_lead_time));
  // Although the connection has disconnected, the proxy should still exist.
  EXPECT_TRUE(audio_renderer_sync_.is_bound());
}

}  // namespace media::audio::test
