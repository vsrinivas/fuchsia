// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/hermetic_audio_test.h"

#include "src/lib/fxl/logging.h"

namespace media::audio::test {

std::unique_ptr<HermeticAudioEnvironment> HermeticAudioTest::environment_;

void HermeticAudioTest::SetUpTestSuite() {
  HermeticAudioTest::environment_ = std::make_unique<HermeticAudioEnvironment>();
  ASSERT_TRUE(HermeticAudioTest::environment_) << "Failed to create hermetic environment";
}

void HermeticAudioTest::TearDownTestSuite() { HermeticAudioTest::environment_ = nullptr; }

//
void HermeticAudioCoreTest::SetUp() {
  HermeticAudioTest::SetUp();

  environment()->ConnectToService(audio_core_.NewRequest());
  audio_core_.set_error_handler(ErrorHandler());
}

void HermeticAudioCoreTest::TearDown() {
  ASSERT_TRUE(audio_core_.is_bound());
  audio_core_.Unbind();

  HermeticAudioTest::TearDown();
}

void HermeticAudioCoreTest::ExpectCallback() {
  HermeticAudioTest::ExpectCallback();

  EXPECT_TRUE(audio_core_.is_bound());
}

void HermeticAudioCoreTest::ExpectDisconnect() {
  HermeticAudioTest::ExpectDisconnect();

  EXPECT_TRUE(audio_core_.is_bound());
}

}  // namespace media::audio::test
