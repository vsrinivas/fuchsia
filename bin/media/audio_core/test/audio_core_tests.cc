// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

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

//
// Tests of the asynchronous Audio interface.
//
class AudioCoreTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "Audio connection lost. Quitting.";
      error_occurred_ = true;
      QuitLoop();
    });
  }

  static constexpr float kUnityGain = 0.0f;

  // For operations expected to complete, wait five seconds, to avoid flaky test
  // behavior in high-load (high-latency) test environments. Conversely, when we
  // expect a timeout, wait 50 msec (normal response is < 5 msec, usually < 1).
  // These values codify the following priorities (in order):
  // 1) False-positive test failures are expensive and must be eliminated;
  // 2) Having satisfying #1, streamline test-run-time (time=resources=cost);
  // 3) Minimize false-negative test outcomes (undetected regressions).
  static constexpr zx::duration kDurationResponseExpected = zx::msec(5000);
  static constexpr zx::duration kDurationTimeoutExpected = zx::msec(50);

  // Cache the previous systemwide settings for Gain and Mute, and put the
  // system into a known state as the baseline for gain&mute tests.
  // This is split into a separate method, rather than included in SetUp(),
  // because it is not needed for tests that do not change Gain|Mute.
  void SaveState() {
    audio_.events().SystemGainMuteChanged = [this](float gain_db, bool muted) {
      received_gain_db_ = gain_db;
      received_mute_ = muted;
      QuitLoop();
    };

    // When a client connects to Audio, the system enqueues an action to send
    // the newly-connected client a callback with the systemwide Gain|Mute
    // settings. The system executes this action after the client's currently
    // executing task completes. This means that if a client establishes a
    // connection and then registers a SystemGainMuteChanged callback BEFORE
    // returning, this client will subsequently (once the system gets a chance
    // to run) receive an initial notification of Gain|Mute settings at the time
    // of connection. Conversely, if a client DOES return before registering,
    // even after subsequently registering for the event the client has no way
    // of learning the current Gain|Mute settings until they are changed.
    EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));

    prev_system_gain_db_ = received_gain_db_;
    prev_system_mute_ = received_mute_;

    // Now place system into a known state: unity-gain and unmuted.
    if (prev_system_gain_db_ != kUnityGain) {
      audio_->SetSystemGain(kUnityGain);
      EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
    }
    if (prev_system_mute_) {
      audio_->SetSystemMute(false);
      EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
    }

    // Once these callbacks arrive, we are primed and ready to test gain|mute.
    EXPECT_EQ(received_gain_db_, kUnityGain);
    EXPECT_EQ(received_mute_, false);
  }

  // Test is done; restore the previously-saved systemwide Gain|Mute settings.
  // Also, reset the audio output routing policy (as some tests change this).
  // This is split into a separate method, rather than included in TearDown(),
  // because it is not needed for tests that do not change Gain|Mute or routing.
  void RestoreState() {
    // Don't waste time restoring values, if they are already what we want.
    if (received_gain_db_ != prev_system_gain_db_) {
      audio_->SetSystemGain(prev_system_gain_db_);
      RunLoopWithTimeout(kDurationResponseExpected);
    }

    if (received_mute_ != prev_system_mute_) {
      audio_->SetSystemMute(prev_system_mute_);
      RunLoopWithTimeout(kDurationResponseExpected);
    }

    EXPECT_EQ(received_gain_db_, prev_system_gain_db_);
    EXPECT_EQ(received_mute_, prev_system_mute_);

    // Leave this persistent systemwide setting in the default state!
    audio_->SetRoutingPolicy(
        fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);
  }

  void TearDown() override {
    audio_in_.Unbind();
    audio_out_.Unbind();
    audio_.Unbind();

    EXPECT_FALSE(error_occurred_);
  }

  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioOutPtr audio_out_;
  fuchsia::media::AudioInPtr audio_in_;

  float prev_system_gain_db_;
  bool prev_system_mute_;

  float received_gain_db_;
  bool received_mute_;

  bool error_occurred_ = false;
};

constexpr float AudioCoreTest::kUnityGain;
constexpr zx::duration AudioCoreTest::kDurationTimeoutExpected;
constexpr zx::duration AudioCoreTest::kDurationResponseExpected;

// Test creation and interface independence of AudioOut.
TEST_F(AudioCoreTest, CreateAudioOut) {
  // Validate Audio can create AudioOut interface.
  audio_->CreateAudioOut(audio_out_.NewRequest());
  EXPECT_TRUE(audio_out_);

  // Validate that Audio persists without AudioOut.
  audio_out_.Unbind();
  EXPECT_FALSE(audio_out_);
  EXPECT_TRUE(audio_);

  // Validate AudioOut persists after Audio is unbound.
  audio_->CreateAudioOut(audio_out_.NewRequest());
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(audio_out_);
}

// Test creation and interface independence of AudioIn.
TEST_F(AudioCoreTest, CreateAudioIn) {
  // Validate Audio can create AudioIn interface.
  audio_->CreateAudioIn(audio_in_.NewRequest(), false);
  EXPECT_TRUE(audio_in_);

  // Validate that Audio persists without AudioIn.
  audio_in_.Unbind();
  EXPECT_FALSE(audio_in_);
  EXPECT_TRUE(audio_);

  // Validate AudioIn persists after Audio is unbound.
  audio_->CreateAudioIn(audio_in_.NewRequest(), true);
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(audio_in_);
}

// Test setting the systemwide Mute.
TEST_F(AudioCoreTest, SetSystemMute_Basic) {
  SaveState();  // Sets system Gain to 0.0 dB and Mute to false.

  audio_->SetSystemMute(true);
  // Expect: gain-change callback received; Mute is set, Gain is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, kUnityGain);
  EXPECT_TRUE(received_mute_);

  audio_->SetSystemMute(false);
  // Expect: gain-change callback received; Mute is cleared, Gain is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, kUnityGain);
  EXPECT_FALSE(received_mute_);

  RestoreState();  // Put that thing back where it came from....
}

// Test setting the systemwide Gain.
TEST_F(AudioCoreTest, SetSystemGain_Basic) {
  SaveState();  // Sets system Gain to 0.0 dB and Mute to false.

  audio_->SetSystemGain(-11.0f);
  // Expect: gain-change callback received; Gain is updated, Mute is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, -11.0f);
  EXPECT_FALSE(received_mute_);

  audio_->SetSystemMute(true);
  // Expect: gain-change callback received (Mute is now set).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));

  audio_->SetSystemGain(kUnityGain);
  // Expect: gain-change callback received; Gain is updated, Mute is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, kUnityGain);
  EXPECT_TRUE(received_mute_);

  RestoreState();
}

// Test the independence of the systemwide Gain and mute settings.
// Setting the systemwide Gain to kMutedGain -- and changing away from
// kMutedGain -- should have no effect on the systemwide Mute.
TEST_F(AudioCoreTest, SetSystemMute_Independence) {
  SaveState();  // Sets system Gain to 0.0 dB and Mute to false.

  audio_->SetSystemGain(fuchsia::media::MUTED_GAIN);
  // Expect: callback; Gain is mute-equivalent; Mute is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, fuchsia::media::MUTED_GAIN);
  EXPECT_FALSE(received_mute_);

  audio_->SetSystemMute(true);
  // Expect: callback; Mute is set (despite Gain's kMutedGain value).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, fuchsia::media::MUTED_GAIN);
  EXPECT_TRUE(received_mute_);

  audio_->SetSystemGain(-42.0f);
  // Expect: callback; Gain is no longer kMutedGain, but Mute is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, -42.0f);
  EXPECT_TRUE(received_mute_);

  RestoreState();
}

// Test setting the systemwide Mute to the already-set value.
// In these cases, we should receive no gain|mute callback (should timeout).
// Verify this with permutations that include Mute=true and Gain=kMutedGain.
// 'No callback if no change in Mute' should be the case REGARDLESS of Gain.
// This test relies upon Gain-Mute independence verified by previous test.
TEST_F(AudioCoreTest, SetSystemMute_NoCallbackIfNoChange) {
  SaveState();  // Sets system Gain to 0.0 dB and Mute to false.

  audio_->SetSystemMute(true);
  // Expect: gain-change callback received (Mute is now set).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  audio_->SetSystemMute(true);
  // Expect: timeout (no callback); no change to Mute, regardless of Gain.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  audio_->SetSystemGain(fuchsia::media::MUTED_GAIN);
  // Expect: gain-change callback received (even though Mute is set).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, fuchsia::media::MUTED_GAIN);
  EXPECT_TRUE(received_mute_);
  audio_->SetSystemMute(true);
  // Expect: timeout (no callback); no change to Mute, regardless of Gain.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  audio_->SetSystemMute(false);
  // Expect: gain-change callback received; Mute is updated, Gain is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, fuchsia::media::MUTED_GAIN);
  EXPECT_FALSE(received_mute_);
  audio_->SetSystemMute(false);
  // Expect: timeout (no callback); no change to Mute, regardless of Gain.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  audio_->SetSystemGain(kUnityGain);
  // Expect: gain-change callback received; Mute is updated, Gain is unchanged.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(received_gain_db_, kUnityGain);
  EXPECT_FALSE(received_mute_);
  audio_->SetSystemMute(false);
  // Expect: timeout (no callback); no change to Mute, regardless of Gain.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  RestoreState();
}

// Test setting the systemwide Gain to the already-set value.
// In these cases, we should receive no gain|mute callback (should timeout).
// Verify this with permutations that include Mute=true and Gain=kMutedGain.
// 'No callback if no change in Gain' should be the case REGARDLESS of Mute.
// This test relies upon Gain-Mute independence verified by previous test.
TEST_F(AudioCoreTest, SetSystemGain_NoCallbackIfNoChange) {
  SaveState();  // Sets system Gain to 0.0 dB and Mute to false.

  // If setting gain to existing value, we should not receive a callback.
  audio_->SetSystemGain(kUnityGain);
  // Expect: timeout (no callback); no change to Gain.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  audio_->SetSystemMute(true);
  // Expect: gain-change callback received (Mute is now true).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  audio_->SetSystemGain(kUnityGain);
  // Expect: timeout (no callback); no change to Gain, regardlesss of Mute.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  audio_->SetSystemGain(fuchsia::media::MUTED_GAIN);
  // Expect: gain-change callback received (Gain is now kMutedGain).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  audio_->SetSystemGain(fuchsia::media::MUTED_GAIN);
  // Expect: timeout (no callback); no change to Gain, regardlesss of Mute.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  audio_->SetSystemMute(false);
  // Expect: gain-change callback received (Mute is now false).
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  audio_->SetSystemGain(fuchsia::media::MUTED_GAIN);
  // Expect: timeout (no callback); no change to Gain, regardlesss of Mute.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  RestoreState();
}

// Test setting (and re-setting) the audio output routing policy.
TEST_F(AudioCoreTest, SetRoutingPolicy) {
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);
  // Setting policy again should have no effect.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);

  RestoreState();
}

//
// Tests of the synchronous AudioSync interface.
//
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
//
class AudioCoreSyncTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_.NewRequest());
    ASSERT_TRUE(audio_);
  }

  fuchsia::media::AudioSyncPtr audio_;
  fuchsia::media::AudioOutSyncPtr audio_out_;
  fuchsia::media::AudioInSyncPtr audio_in_;
};

// Test creation and interface independence of AudioOut.
TEST_F(AudioCoreSyncTest, CreateAudioOut) {
  // Validate Audio can create AudioOut interface.
  EXPECT_EQ(ZX_OK, audio_->CreateAudioOut(audio_out_.NewRequest()));
  EXPECT_TRUE(audio_out_);

  // Validate that Audio persists without AudioOut.
  audio_out_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate AudioOut persists after Audio is unbound.
  EXPECT_EQ(ZX_OK, audio_->CreateAudioOut(audio_out_.NewRequest()));
  audio_ = nullptr;
  EXPECT_TRUE(audio_out_);
}

// Test creation and interface independence of AudioIn.
TEST_F(AudioCoreSyncTest, CreateAudioIn) {
  // Validate Audio can create AudioIn interface.
  EXPECT_EQ(ZX_OK, audio_->CreateAudioIn(audio_in_.NewRequest(), true));
  EXPECT_TRUE(audio_in_);

  // Validate that Audio persists without AudioIn.
  audio_in_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate AudioIn persists after Audio is unbound.
  audio_->CreateAudioIn(audio_in_.NewRequest(), false);
  audio_ = nullptr;
  EXPECT_TRUE(audio_in_);
}

// Test the setting of audio output routing policy.
TEST_F(AudioCoreSyncTest, SetRoutingPolicy) {
  // Validate Audio can set last-plugged routing policy synchronously.
  EXPECT_EQ(ZX_OK,
            audio_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));

  // Validate Audio can set all-outputs routing policy synchronously.
  EXPECT_EQ(ZX_OK,
            audio_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS));

  // This is a persistent systemwide setting. Leave system in the default state!
  EXPECT_EQ(ZX_OK,
            audio_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));
}

// TODO(mpuryear): If we ever add functionality such as parameter parsing,
// relocate the below along with it, to a separate main.cc file.
//
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  return result;
}

}  // namespace test
}  // namespace audio
}  // namespace media
