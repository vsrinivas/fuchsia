// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/lib/analysis/glitch.h"

#include <cmath>

#include <gtest/gtest.h>

#include "src/media/audio/lib/analysis/generators.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio {

TEST(GlitchDetectors, SlopeChecker) {
  constexpr int32_t kTestFreq = 440;
  constexpr int32_t kSamplesPerSecond = 48000;
  auto format = Format::Create<ASF::FLOAT>(1, kSamplesPerSecond).take_value();

  // Check a good signal, then inject a glitch and check that's detected.
  auto buf = GenerateCosineAudio(format, kTestFreq, 1);
  SlopeChecker checker(kSamplesPerSecond, kTestFreq);
  for (auto s : buf.samples()) {
    EXPECT_TRUE(checker.Check(s, true));
  }

  buf.samples()[220] += 0.1f;

  checker.Reset();

  EXPECT_TRUE(checker.Check(buf.samples()[219]));
  EXPECT_FALSE(checker.Check(buf.samples()[220]));
  EXPECT_FALSE(checker.Check(buf.samples()[221]));
  EXPECT_TRUE(checker.Check(buf.samples()[222]));
}

}  // namespace media::audio
