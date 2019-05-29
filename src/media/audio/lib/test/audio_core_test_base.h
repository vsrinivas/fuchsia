// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_CORE_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_CORE_TEST_BASE_H_

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/lib/test/audio_test_base.h"

namespace media::audio::test {

// TODO(mpuryear): move this (and mixer's duplicate) to gain_control.fidl
constexpr float kUnityGainDb = 0.0f;
constexpr float kTooLowGainDb = fuchsia::media::audio::MUTED_GAIN_DB - 0.1f;
constexpr float kTooHighGainDb = fuchsia::media::audio::MAX_GAIN_DB + 0.1f;

//
// AudioCoreTestBase
//
// This set of tests verifies asynchronous usage of audio_core protocols.
class AudioCoreTestBase : public AudioTestBase {
 protected:
  void SetUp() override;
  void TearDown() override;

  void ExpectCondition(fit::function<bool()> condition) override;
  void ExpectCallback() override;
  void ExpectDisconnect() override;

  fuchsia::media::AudioCorePtr audio_core_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_CORE_TEST_BASE_H_
