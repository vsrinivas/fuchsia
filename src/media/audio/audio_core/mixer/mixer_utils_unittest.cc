// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/mixer_utils.h"

#include <iterator>
#include <limits>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

namespace media::audio {
namespace {

//
// SampleScaler tests (four scale types)
//
// Validate that with MUTED scale type, all output is silence (0).
TEST(SampleScalerTest, Mute) {
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.5f};
  const float expect = 0.0f;

  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::MUTED>::Scale(input[0], scale[0]), expect);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::MUTED>::Scale(input[0], scale[1]), expect);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::MUTED>::Scale(input[1], scale[0]), expect);
}

// Validate that with NE_UNITY scale types, output is scaled appropriately.
TEST(SampleScalerTest, NotUnity) {
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.5f};
  const float expect[] = {-0.75f, -0.25f, 1.5f};

  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::NE_UNITY>::Scale(input[0], scale[0]), expect[0]);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::NE_UNITY>::Scale(input[0], scale[1]), expect[1]);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::NE_UNITY>::Scale(input[1], scale[0]), expect[2]);
}

// Validate that RAMPING scale type scales appropriately and is identical to NE_UNITY.
TEST(SampleScalerTest, Ramping) {
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.5f};
  const float expect[] = {-0.75f, -0.25f, 1.5f};

  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::RAMPING>::Scale(input[0], scale[0]), expect[0]);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::RAMPING>::Scale(input[0], scale[1]), expect[1]);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::RAMPING>::Scale(input[1], scale[0]), expect[2]);

  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::RAMPING>::Scale(input[0], scale[0]),
            mixer::SampleScaler<mixer::ScalerType::NE_UNITY>::Scale(input[0], scale[0]));
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::RAMPING>::Scale(input[0], scale[1]),
            mixer::SampleScaler<mixer::ScalerType::NE_UNITY>::Scale(input[0], scale[1]));
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::RAMPING>::Scale(input[1], scale[0]),
            mixer::SampleScaler<mixer::ScalerType::NE_UNITY>::Scale(input[1], scale[0]));
}

// Validate that with EQ_UNITY scale type, all output is same as input.
TEST(SampleScalerTest, Unity) {
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.5f};

  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::EQ_UNITY>::Scale(input[0], scale[0]), input[0]);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::EQ_UNITY>::Scale(input[0], scale[1]), input[0]);
  EXPECT_EQ(mixer::SampleScaler<mixer::ScalerType::EQ_UNITY>::Scale(input[1], scale[0]), input[1]);
}

//
// DestMixer tests focus primarily on accumulate functionality, since DestMixer internally uses
// SampleScaler which is validated above.
//
// MUTED never contributes the new sample to the mix. Both accum and no-accum options are validated.
TEST(DestMixerTest, Mute) {
  using DmNoAccum = mixer::DestMixer<mixer::ScalerType::MUTED, false>;
  using DmAccum = mixer::DestMixer<mixer::ScalerType::MUTED, true>;

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

// NE_UNITY scales a new sample as it is added to the mix. In this scope, RAMPING behaves
// identically to NE_UNITY. Both accumulate and no-accumulate options are validated.
TEST(DestMixerTest, NeUnity) {
  const float prev = -0.1f;
  const float input[] = {-0.5f, 1.0f};
  const Gain::AScale scale[] = {1.5f, 0.75f};
  const float expect[] = {-0.75f, -0.375f, 1.5f, 0.75f};

  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, false>::Mix(prev, input[0], scale[0])),
            expect[0]);
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, false>::Mix(prev, input[0], scale[1])),
            expect[1]);
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, false>::Mix(prev, input[1], scale[0])),
            expect[2]);

  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, true>::Mix(prev, input[0], scale[0])),
            expect[0] + prev);
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, true>::Mix(prev, input[0], scale[1])),
            expect[1] + prev);
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, true>::Mix(prev, input[1], scale[1])),
            expect[3] + prev);

  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, true>::Mix(prev, input[0], scale[0])),
            (mixer::DestMixer<mixer::ScalerType::RAMPING, true>::Mix(prev, input[0], scale[0])));
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, true>::Mix(prev, input[1], scale[0])),
            (mixer::DestMixer<mixer::ScalerType::RAMPING, true>::Mix(prev, input[1], scale[0])));
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, true>::Mix(prev, input[1], scale[1])),
            (mixer::DestMixer<mixer::ScalerType::RAMPING, true>::Mix(prev, input[1], scale[1])));

  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, false>::Mix(prev, input[0], scale[1])),
            (mixer::DestMixer<mixer::ScalerType::RAMPING, false>::Mix(prev, input[0], scale[1])));
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, false>::Mix(prev, input[1], scale[0])),
            (mixer::DestMixer<mixer::ScalerType::RAMPING, false>::Mix(prev, input[1], scale[0])));
  EXPECT_EQ((mixer::DestMixer<mixer::ScalerType::NE_UNITY, false>::Mix(prev, input[1], scale[1])),
            (mixer::DestMixer<mixer::ScalerType::RAMPING, false>::Mix(prev, input[1], scale[1])));
}

// UNITY will not scale a sample as it adds it to the mix. Validate both accumulate and no-accum.
TEST(DestMixerTest, Unity) {
  using DmNoAccum = mixer::DestMixer<mixer::ScalerType::EQ_UNITY, false>;
  using DmAccum = mixer::DestMixer<mixer::ScalerType::EQ_UNITY, true>;

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
