// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/mixer_utils.h"

#include <iterator>
#include <limits>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {
namespace {

//
// DestMixer tests focus primarily on accumulate functionality, since DestMixer internally uses
// GainType which is validated above.
//
// kSilent never contributes the new sample to the mix. Both accum and no-accum options are
// validated.
TEST(DestMixerTest, Mute) {
  using DmNoAccum = mixer::DestMixer<media_audio::GainType::kSilent, false>;
  using DmAccum = mixer::DestMixer<media_audio::GainType::kSilent, true>;

  const float prev = -0.1f;
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.75f};
  const float expect = 0.0;

  EXPECT_EQ(DmNoAccum::Mix(prev, input[0], scale[0]), expect);
  EXPECT_EQ(DmNoAccum::Mix(prev, input[0], scale[1]), expect);
  EXPECT_EQ(DmNoAccum::Mix(prev, input[1], scale[0]), expect);
  EXPECT_EQ(DmNoAccum::Mix(prev, input[1], scale[1]), expect);

  EXPECT_EQ(DmAccum::Mix(prev, input[0], scale[0]), expect + prev);
  EXPECT_EQ(DmAccum::Mix(prev, input[0], scale[1]), expect + prev);
  EXPECT_EQ(DmAccum::Mix(prev, input[1], scale[0]), expect + prev);
  EXPECT_EQ(DmAccum::Mix(prev, input[1], scale[1]), expect + prev);
}

// kNonUnity scales a new sample as it is added to the mix. In this scope, kRamping behaves
// identically to kNonUnity. Both accumulate and no-accumulate options are validated.
TEST(DestMixerTest, NeUnity) {
  const float prev = -0.1f;
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.75f};
  const float expect[] = {-0.75f, -0.375f, 1.5f, 0.75f};

  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, false>::Mix(prev, input[0], scale[0])),
      expect[0]);
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, false>::Mix(prev, input[0], scale[1])),
      expect[1]);
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, false>::Mix(prev, input[1], scale[0])),
      expect[2]);

  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, true>::Mix(prev, input[0], scale[0])),
      expect[0] + prev);
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, true>::Mix(prev, input[0], scale[1])),
      expect[1] + prev);
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, true>::Mix(prev, input[1], scale[1])),
      expect[3] + prev);

  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, true>::Mix(prev, input[0], scale[0])),
      (mixer::DestMixer<media_audio::GainType::kRamping, true>::Mix(prev, input[0], scale[0])));
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, true>::Mix(prev, input[1], scale[0])),
      (mixer::DestMixer<media_audio::GainType::kRamping, true>::Mix(prev, input[1], scale[0])));
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, true>::Mix(prev, input[1], scale[1])),
      (mixer::DestMixer<media_audio::GainType::kRamping, true>::Mix(prev, input[1], scale[1])));

  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, false>::Mix(prev, input[0], scale[1])),
      (mixer::DestMixer<media_audio::GainType::kRamping, false>::Mix(prev, input[0], scale[1])));
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, false>::Mix(prev, input[1], scale[0])),
      (mixer::DestMixer<media_audio::GainType::kRamping, false>::Mix(prev, input[1], scale[0])));
  EXPECT_EQ(
      (mixer::DestMixer<media_audio::GainType::kNonUnity, false>::Mix(prev, input[1], scale[1])),
      (mixer::DestMixer<media_audio::GainType::kRamping, false>::Mix(prev, input[1], scale[1])));
}

// UNITY will not scale a sample as it adds it to the mix. Validate both accumulate and no-accum.
TEST(DestMixerTest, Unity) {
  using DmNoAccum = mixer::DestMixer<media_audio::GainType::kUnity, false>;
  using DmAccum = mixer::DestMixer<media_audio::GainType::kUnity, true>;

  const float prev = -0.1f;
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.75f};

  EXPECT_EQ(DmNoAccum::Mix(prev, input[0], scale[0]), input[0]);
  EXPECT_EQ(DmNoAccum::Mix(prev, input[0], scale[1]), input[0]);
  EXPECT_EQ(DmNoAccum::Mix(prev, input[1], scale[0]), input[1]);
  EXPECT_EQ(DmNoAccum::Mix(prev, input[1], scale[1]), input[1]);

  EXPECT_EQ(DmAccum::Mix(prev, input[0], scale[0]), input[0] + prev);
  EXPECT_EQ(DmAccum::Mix(prev, input[0], scale[1]), input[0] + prev);
  EXPECT_EQ(DmAccum::Mix(prev, input[1], scale[0]), input[1] + prev);
  EXPECT_EQ(DmAccum::Mix(prev, input[1], scale[1]), input[1] + prev);
}

}  // namespace
}  // namespace media::audio
