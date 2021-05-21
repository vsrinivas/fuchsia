// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {
namespace {

class StubMixer : public Mixer {
 public:
  StubMixer() : Mixer(Fixed(0), Fixed(0), Gain::Limits{}) {}

  bool Mix(float*, int64_t, int64_t*, const void*, int64_t, Fixed*, bool) final { return false; }
};

TEST(BookkeepingTest, Defaults) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();

  EXPECT_EQ(bookkeeping.step_size, kOneFrame);
  EXPECT_EQ(bookkeeping.rate_modulo(), 0ull);
  EXPECT_EQ(bookkeeping.denominator(), 1ull);
  EXPECT_EQ(bookkeeping.source_pos_modulo, 0ull);

  EXPECT_TRUE(bookkeeping.gain.IsUnity());
  EXPECT_FALSE(bookkeeping.gain.IsSilent());
  EXPECT_FALSE(bookkeeping.gain.IsRamping());
}

// Upon Reset, Bookkeeping should clear position modulo. It does not complete in-progress ramps.
TEST(BookkeepingTest, Reset) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.SetRateModuloAndDenominator(5, 7);
  bookkeeping.source_pos_modulo = 3;
  bookkeeping.gain.SetSourceGainWithRamp(-42.0f, zx::sec(1),
                                         fuchsia::media::audio::RampType::SCALE_LINEAR);
  EXPECT_TRUE(bookkeeping.gain.IsRamping());

  bookkeeping.Reset();

  // Rate should not be changed, but position modulo should be.
  EXPECT_EQ(bookkeeping.rate_modulo(), 5ull);
  EXPECT_EQ(bookkeeping.denominator(), 7ull);
  EXPECT_TRUE(bookkeeping.gain.IsRamping());

  EXPECT_EQ(bookkeeping.source_pos_modulo, 0ull);
}

}  // namespace
}  // namespace media::audio::mixer
