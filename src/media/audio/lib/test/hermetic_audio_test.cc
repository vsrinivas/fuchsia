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

void HermeticAudioTest::SetUp() {
  TestFixture::SetUp();
  ASSERT_TRUE(environment()->EnsureStart(this));
}

}  // namespace media::audio::test
