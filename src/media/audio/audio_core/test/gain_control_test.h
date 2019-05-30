// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_GAIN_CONTROL_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_GAIN_CONTROL_TEST_H_

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/lib/test/audio_core_test_base.h"

namespace media::audio::test {

// GainControlTestBase
//
// This set of tests verifies asynchronous usage of GainControl.
class GainControlTestBase : public AudioCoreTestBase {
 protected:
  void TearDown() final;

  void SetNegativeExpectations() override;
  void SetUpRenderer();
  void SetUpCapturer();
  void SetUpRenderer2();
  void SetUpCapturer2();
  void SetUpGainControl();
  void SetUpGainControlOnRenderer();
  void SetUpGainControlOnCapturer();
  void SetUpGainControl2();
  void SetUpGainControl2OnRenderer();
  void SetUpGainControl2OnCapturer();
  void SetUpGainControl2OnRenderer2();
  void SetUpGainControl2OnCapturer2();

  // Always augmented by child implementations that set up the API interface.
  virtual bool ApiIsNull() = 0;

  void SetGain(float gain_db);
  void SetMute(bool mute);

  // Tests expect a gain callback. Absorb this; perform related error checking.
  virtual void ExpectGainCallback(float gain_db, bool mute);

  // Tests expect the API binding to disconnect, then the GainControl binding as
  // well. After the first disconnect, assert that GainControl is still bound.
  void ExpectDisconnect() override;

  // Core test cases that are validated across various scenarios
  void TestSetGain();
  void TestSetMute();
  void TestSetGainMute();
  void TestDuplicateSetGain();
  void TestDuplicateSetMute();
  void TestSetGainTooHigh();
  void TestSetGainTooLow();
  void TestSetGainNaN();

  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::audio::GainControlPtr gain_control_;

  float received_gain_db_ = kTooLowGainDb;
  bool received_mute_ = false;

  // Member variables for tests that use multiple interface bindings
  bool error_occurred_2_ = false;
  fuchsia::media::AudioRendererPtr audio_renderer_2_;
  fuchsia::media::AudioCapturerPtr audio_capturer_2_;
  fuchsia::media::audio::GainControlPtr gain_control_2_;

  float received_gain_db_2_ = kTooLowGainDb;
  bool received_mute_2_ = false;

  // Member variables to manage our expectations
  bool null_api_expected_ = false;
  bool null_gain_control_expected_ = false;
  bool null_gain_control_expected_2_ = true;
  bool error_expected_2_ = false;
};

// RenderGainControlTest
//
class RenderGainControlTest : public GainControlTestBase {
 protected:
  void SetUp() override;
  bool ApiIsNull() final { return !audio_renderer_.is_bound(); }
};

// CaptureGainControlTest
//
class CaptureGainControlTest : public GainControlTestBase {
 protected:
  void SetUp() override;
  bool ApiIsNull() final { return !audio_capturer_.is_bound(); }
};

// SiblingGainControlsTest
//
// On a renderer/capturer, sibling GainControls receive identical notifications.
class SiblingGainControlsTest : public GainControlTestBase {
 protected:
  void SetNegativeExpectations() override;

  // Absorb a gain callback from the sibling GainControl as well.
  void ExpectGainCallback(float gain_db, bool mute) final;

  // Absorb the second GainControl's disconnect, once the first disconnects.
  void ExpectDisconnect() final;
};

// RendererTwoGainControlsTest
//
// Verify that Renderer's second GainControl receives the same notifications.
class RendererTwoGainControlsTest : public SiblingGainControlsTest {
 protected:
  void SetUp() override;
  bool ApiIsNull() final { return !audio_renderer_.is_bound(); }
};

// CapturerTwoGainControlsTest
//
// Verify that Capturer's second GainControl receives the same notifications.
class CapturerTwoGainControlsTest : public SiblingGainControlsTest {
 protected:
  void SetUp() override;
  bool ApiIsNull() final { return !audio_capturer_.is_bound(); }
};

// IndependentGainControlsTest
//
// Verify that GainControls on different API instances are fully independent.
class IndependentGainControlsTest : public GainControlTestBase {
 protected:
  // Expect nothing from the independent gain control.
  void ExpectGainCallback(float gain_db, bool mute) final;

  // Expect NO disconnect from our independent gain control -- after the first
  // gain control disconnect has already occurred.
  void ExpectDisconnect() final;
};

// TwoRenderersGainControlsTest
//
// Verify that Renderers' GainControls are fully independent.
class TwoRenderersGainControlsTest : public IndependentGainControlsTest {
 protected:
  void SetUp() override;
  bool ApiIsNull() final {
    return !audio_renderer_.is_bound() && audio_renderer_2_.is_bound();
  }
};

// RendererCapturerGainControlsTest
//
// Verify that Renderer GainControl does not affect Capturer GainControl.
class RendererCapturerGainControlsTest : public IndependentGainControlsTest {
 protected:
  void SetUp() override;
  bool ApiIsNull() final {
    return !audio_renderer_.is_bound() && audio_capturer_.is_bound();
  }
};

// CapturerRendererGainControlsTest
//
// Verify that Capturer GainControl does not affect Renderer GainControl.
class CapturerRendererGainControlsTest : public IndependentGainControlsTest {
 protected:
  void SetUp() override;
  bool ApiIsNull() final {
    return !audio_capturer_.is_bound() && audio_renderer_.is_bound();
  }
};

// TwoCapturersGainControlsTest
//
// Verify that Capturers' GainControls are fully independent.
class TwoCapturersGainControlsTest : public IndependentGainControlsTest {
 protected:
  void SetUp() override;
  bool ApiIsNull() final {
    return !audio_capturer_.is_bound() && audio_capturer_2_.is_bound();
  }
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_GAIN_CONTROL_TEST_H_
