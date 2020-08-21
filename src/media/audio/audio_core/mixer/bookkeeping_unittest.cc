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
  StubMixer() : Mixer(0, 0) {}

  bool Mix(float*, uint32_t, uint32_t*, const void*, uint32_t, int32_t*, bool) final {
    return false;
  }
};

TEST(BookkeepingTest, Defaults) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();

  EXPECT_EQ(bookkeeping.step_size, Mixer::FRAC_ONE);
  EXPECT_EQ(bookkeeping.rate_modulo, 0ull);
  EXPECT_EQ(bookkeeping.denominator, 0ull);
  EXPECT_EQ(bookkeeping.src_pos_modulo, 0ull);

  EXPECT_TRUE(bookkeeping.gain.IsUnity());
  EXPECT_FALSE(bookkeeping.gain.IsSilent());
  EXPECT_FALSE(bookkeeping.gain.IsRamping());
}

// Upon Reset, Bookkeeping should clear position modulo and gain ramp. It should also clear its
// historical dest and source frame counters.
TEST(BookkeepingTest, Reset) {
  StubMixer mixer;
  auto& bookkeeping = mixer.bookkeeping();
  bookkeeping.rate_modulo = 5;
  bookkeeping.denominator = 7;
  bookkeeping.src_pos_modulo = 3;
  bookkeeping.gain.SetSourceGainWithRamp(-42.0f, zx::sec(1),
                                         fuchsia::media::audio::RampType::SCALE_LINEAR);
  EXPECT_TRUE(bookkeeping.gain.IsRamping());

  bookkeeping.Reset();

  EXPECT_EQ(bookkeeping.rate_modulo, 5ull);
  EXPECT_EQ(bookkeeping.denominator, 7ull);
  EXPECT_EQ(bookkeeping.src_pos_modulo, 0ull);

  EXPECT_FALSE(bookkeeping.gain.IsRamping());
}

}  // namespace
}  // namespace media::audio::mixer
