// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/audio_core_test_base.h"

namespace media::audio::test {

//
// AudioCoreTestBase implementation
//
void AudioCoreTestBase::SetUp() {
  AudioTestBase::SetUp();

  startup_context_->svc()->Connect(audio_core_.NewRequest());
  audio_core_.set_error_handler(ErrorHandler());
}

void AudioCoreTestBase::TearDown() {
  ASSERT_TRUE(audio_core_.is_bound());
  audio_core_.Unbind();

  AudioTestBase::TearDown();
}

void AudioCoreTestBase::ExpectCondition(fit::function<bool()> condition) {
  AudioTestBase::ExpectCondition(std::move(condition));

  EXPECT_TRUE(audio_core_.is_bound());
}

void AudioCoreTestBase::ExpectCallback() {
  AudioTestBase::ExpectCallback();

  EXPECT_TRUE(audio_core_.is_bound());
}

void AudioCoreTestBase::ExpectDisconnect() {
  AudioTestBase::ExpectDisconnect();

  EXPECT_TRUE(audio_core_.is_bound());
}

}  // namespace media::audio::test
