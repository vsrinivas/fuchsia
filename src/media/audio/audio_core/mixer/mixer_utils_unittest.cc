// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/mixer_utils.h"

#include <limits>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/constants.h"

namespace media::audio {
namespace {

//
// SampleNormalizer converts between uint8/int16/int24-in-32 and our internal float format.
//
// Validate uint8->float format conversion
TEST(SampleNormalizerTest, UInt8_Basic) {
  const uint8_t data[] = {0x00, 0x40, 0x80, 0xE0};
  const float expect[] = {-1.0, -0.5, 0, 0.75};

  for (auto i = 0u; i < fbl::count_of(data); ++i) {
    EXPECT_EQ(mixer::SampleNormalizer<uint8_t>::Read(data + i), expect[i]);
  }

  const uint8_t max_val = 0xFF;
  EXPECT_LT(mixer::SampleNormalizer<uint8_t>::Read(&max_val), 1.0f);
  EXPECT_GT(mixer::SampleNormalizer<uint8_t>::Read(&max_val), 0.99f);
}

// Validate int16->float format conversion
TEST(SampleNormalizerTest, Int16_Basic) {
  const int16_t data[] = {std::numeric_limits<int16_t>::min(), -0x4000, 0, 0x6000};
  const float expect[] = {-1.0, -0.5, 0, 0.75};

  for (auto i = 0u; i < fbl::count_of(data); ++i) {
    EXPECT_EQ(mixer::SampleNormalizer<int16_t>::Read(data + i), expect[i]);
  }

  const int16_t max_val = 0x7FFF;
  EXPECT_LT(mixer::SampleNormalizer<int16_t>::Read(&max_val), 1.0f);
  EXPECT_GT(mixer::SampleNormalizer<int16_t>::Read(&max_val), 0.9999f);
}

// Validate int24->float format conversion
TEST(SampleNormalizerTest, Int24_Basic) {
  const int32_t data[] = {kMinInt24In32, -0x40000000, 0, 0x60000000};
  const float expect[] = {-1.0, -0.5, 0, 0.75};

  for (auto i = 0u; i < fbl::count_of(data); ++i) {
    EXPECT_EQ(mixer::SampleNormalizer<int32_t>::Read(data + i), expect[i]);
  }

  const int32_t max_val = kMaxInt24In32;
  EXPECT_LT(mixer::SampleNormalizer<int32_t>::Read(&max_val), 1.0f);
  EXPECT_GT(mixer::SampleNormalizer<int32_t>::Read(&max_val), 0.999999f);
}

// Validate float->float format conversion
TEST(SampleNormalizerTest, Float_Basic) {
  const float data[] = {-1.0, -0.5, 0, 0.75, 1.0};

  for (auto i = 0u; i < fbl::count_of(data); ++i) {
    EXPECT_EQ(mixer::SampleNormalizer<float>::Read(data + i), data[i]);
  }
}

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
// SrcReader tests all use float, as type conversion is handled by SamplerNormalizer
//
// Validate N->N channel mapping, including higher channel counts.
// Expectation: each source channel maps identically to that destination channel.
TEST(SrcReaderTest, Map_N_N) {
  const float data[] = {-1.0, 1.0, 0.0, 0.5};

  EXPECT_EQ((mixer::SrcReader<float, 1, 1>::Read(data, 0)), data[0]);
  EXPECT_EQ((mixer::SrcReader<float, 1, 1>::Read(data + 3, 0)), data[3]);

  EXPECT_EQ((mixer::SrcReader<float, 2, 2>::Read(data, 0)), data[0]);
  EXPECT_EQ((mixer::SrcReader<float, 2, 2>::Read(data, 1)), data[1]);
  EXPECT_EQ((mixer::SrcReader<float, 2, 2>::Read(data + 2, 0)), data[2]);
  EXPECT_EQ((mixer::SrcReader<float, 2, 2>::Read(data + 2, 1)), data[3]);

  EXPECT_EQ((mixer::SrcReader<float, 3, 3>::Read(data, 0)), data[0]);
  EXPECT_EQ((mixer::SrcReader<float, 3, 3>::Read(data, 1)), data[1]);
  EXPECT_EQ((mixer::SrcReader<float, 3, 3>::Read(data + 2, 0)), data[2]);
  EXPECT_EQ((mixer::SrcReader<float, 3, 3>::Read(data + 1, 2)), data[3]);

  EXPECT_EQ((mixer::SrcReader<float, 4, 4>::Read(data, 0)), data[0]);
  EXPECT_EQ((mixer::SrcReader<float, 4, 4>::Read(data, 1)), data[1]);
  EXPECT_EQ((mixer::SrcReader<float, 4, 4>::Read(data, 2)), data[2]);
  EXPECT_EQ((mixer::SrcReader<float, 4, 4>::Read(data, 3)), data[3]);

  EXPECT_EQ((mixer::SrcReader<float, 6, 6>::Read(data, 1)), data[1]);
  EXPECT_EQ((mixer::SrcReader<float, 6, 6>::Read(data, 2)), data[2]);

  EXPECT_EQ((mixer::SrcReader<float, 8, 8>::Read(data, 0)), data[0]);
  EXPECT_EQ((mixer::SrcReader<float, 8, 8>::Read(data, 3)), data[3]);
}

// Validate 1->N channel mapping, including higher destination channel counts.
// Expectation: the one source channel maps to every destination channel without attenuation.
TEST(SrcReaderTest, Map_1_N) {
  const float data[] = {0.76543f, 0.0};

  EXPECT_EQ((mixer::SrcReader<float, 1, 1>::Read(data, 0)), *data);

  EXPECT_EQ((mixer::SrcReader<float, 1, 2>::Read(data, 0)), *data);
  EXPECT_EQ((mixer::SrcReader<float, 1, 2>::Read(data, 1)),
            (mixer::SrcReader<float, 1, 2>::Read(data, 0)));

  EXPECT_EQ((mixer::SrcReader<float, 1, 3>::Read(data, 0)), *data);
  EXPECT_EQ((mixer::SrcReader<float, 1, 3>::Read(data, 1)),
            (mixer::SrcReader<float, 1, 3>::Read(data, 0)));
  EXPECT_EQ((mixer::SrcReader<float, 1, 3>::Read(data, 2)),
            (mixer::SrcReader<float, 1, 3>::Read(data, 0)));

  EXPECT_EQ((mixer::SrcReader<float, 1, 4>::Read(data, 0)), *data);
  EXPECT_EQ((mixer::SrcReader<float, 1, 4>::Read(data, 1)),
            (mixer::SrcReader<float, 1, 4>::Read(data, 0)));
  EXPECT_EQ((mixer::SrcReader<float, 1, 4>::Read(data, 2)),
            (mixer::SrcReader<float, 1, 4>::Read(data, 0)));
  EXPECT_EQ((mixer::SrcReader<float, 1, 4>::Read(data, 3)),
            (mixer::SrcReader<float, 1, 4>::Read(data, 0)));

  EXPECT_EQ((mixer::SrcReader<float, 1, 5>::Read(data, 1)), *data);
  EXPECT_EQ((mixer::SrcReader<float, 1, 5>::Read(data, 4)),
            (mixer::SrcReader<float, 1, 5>::Read(data, 1)));

  EXPECT_EQ((mixer::SrcReader<float, 1, 8>::Read(data, 2)), *data);
  EXPECT_EQ((mixer::SrcReader<float, 1, 8>::Read(data, 7)),
            (mixer::SrcReader<float, 1, 8>::Read(data, 2)));
}

// Validate 2->1 channel mapping.
// Expectation: each source channel should contribute equally to the one destination channel.
// The one destination channel is average of all source channels.
TEST(SrcReaderTest, Map_2_1) {
  using SR = mixer::SrcReader<float, 2, 1>;
  const float data[] = {-1.0, 1.0, 0.5};
  const float expect[] = {0, 0.75};

  EXPECT_EQ(SR::Read(data, 0), expect[0]);
  EXPECT_EQ(SR::Read(data + 1, 0), expect[1]);
}

// Validate 2->3 channel mapping.
// Expectation: 3-channel destination is L.R.C (or some other geometry where third destination
// channel should contain an equal mix of the two source channels).
// dest[0] is source[0]; dest[1] is source[1]; dest[2] is average of source[0] and source[1].
TEST(SrcReaderTest, Map_2_3) {
  using SR = mixer::SrcReader<float, 2, 3>;
  const float data[] = {-1.0, 1.0, 0.5};
  const float expect_chan2[] = {0.0, 0.75};

  EXPECT_EQ(SR::Read(data, 0), data[0]);
  EXPECT_EQ(SR::Read(data, 1), data[1]);
  EXPECT_EQ(SR::Read(data, 2), expect_chan2[0]);

  EXPECT_EQ(SR::Read(data + 1, 0), data[1]);
  EXPECT_EQ(SR::Read(data + 1, 1), data[2]);
  EXPECT_EQ(SR::Read(data + 1, 2), expect_chan2[1]);
}

// Validate 2->4 channel mapping.
// Expectation: 4-chan destination is "4 corners" FL.FR.BL.BR (or other L.R.L.R geometry).
// We map each source channel equally to the two destination channels on each side.
TEST(SrcReaderTest, Map_2_4) {
  using SR = mixer::SrcReader<float, 2, 4>;
  const float data[] = {-1.0, 1.0, 0.5};

  EXPECT_EQ(SR::Read(data, 0), data[0]);
  EXPECT_EQ(SR::Read(data, 1), data[1]);
  EXPECT_EQ(SR::Read(data, 2), SR::Read(data, 0));
  EXPECT_EQ(SR::Read(data, 3), SR::Read(data, 1));

  EXPECT_EQ(SR::Read(data + 1, 0), data[1]);
  EXPECT_EQ(SR::Read(data + 1, 1), data[2]);
  EXPECT_EQ(SR::Read(data + 1, 2), SR::Read(data + 1, 0));
  EXPECT_EQ(SR::Read(data + 1, 3), SR::Read(data + 1, 1));
}

// Validate 3->1 channel mapping.
// Expectation: each source channel should contribute equally to the one destination channel.
// The one destination channel is average of all source channels.
TEST(SrcReaderTest, Map_3_1) {
  using SR = mixer::SrcReader<float, 3, 1>;
  const float data[] = {-0.5, 1.0, 1.0, -0.8};
  const float expect[] = {0.5, 0.4};

  EXPECT_EQ(SR::Read(data, 0), expect[0]);
  EXPECT_EQ(SR::Read(data, 1), SR::Read(data, 0));
  EXPECT_EQ(SR::Read(data, 2), SR::Read(data, 0));

  EXPECT_EQ(SR::Read(data + 1, 0), expect[1]);
  EXPECT_EQ(SR::Read(data + 1, 1), SR::Read(data + 1, 0));
  EXPECT_EQ(SR::Read(data + 1, 2), SR::Read(data + 1, 0));
}

// Validate 3->2 channel mapping.
// Expectation: 3-channel source is L.R.C (or some other geometry where third source channel should
// be distributed evenly into both destination channels).
//
// Conceptually, dest[0] becomes source[0] + source[2]/2; dest[1] becomes source[1] + source[2]/2.
// However when contributing source[2] to two destinations, we must conserve the POWER of
// source[2] relative to the other source channels -- we add sqr(0.5)*source[2] (not 0.5*source[2])
// to each side -- and then normalize the result to eliminate clipping.
//
//   dest[0] = (0.585786... * source[0]) + (0.414213... * source[2])
//   dest[1] = (0.585786... * source[1]) + (0.414213... * source[2])
TEST(SrcReaderTest, Map_3_2) {
  using SR = mixer::SrcReader<float, 3, 2>;
  const float data[] = {1, -0.5, -0.5, -1};
  const float expect[] = {0.378679656f, -0.5f, -0.70710678f};

  EXPECT_FLOAT_EQ(SR::Read(data, 0), expect[0]);
  EXPECT_FLOAT_EQ(SR::Read(data, 1), expect[1]);

  EXPECT_FLOAT_EQ(SR::Read(data + 1, 0), expect[2]);
  EXPECT_EQ(SR::Read(data + 1, 1), SR::Read(data + 1, 0));
}

// No built-in 3->4 mapping is provided

// Validate 4->1 channel mapping.
// Expectation: each source channel should contribute equally to the one destination channel.
// The one destination channel is average of all source channels.
TEST(SrcReaderTest, Map_4_1) {
  using SR = mixer::SrcReader<float, 4, 1>;
  const float data[] = {-0.25, 0.75, 1.0, -0.5, -0.05};
  const float expect[] = {0.25, 0.3};

  EXPECT_EQ(SR::Read(data, 0), expect[0]);
  EXPECT_EQ(SR::Read(data, 1), SR::Read(data, 0));
  EXPECT_EQ(SR::Read(data, 2), SR::Read(data, 0));
  EXPECT_EQ(SR::Read(data, 3), SR::Read(data, 0));

  EXPECT_EQ(SR::Read(data + 1, 0), expect[1]);
  EXPECT_EQ(SR::Read(data + 1, 1), SR::Read(data + 1, 0));
  EXPECT_EQ(SR::Read(data + 1, 2), SR::Read(data + 1, 0));
  EXPECT_EQ(SR::Read(data + 1, 3), SR::Read(data + 1, 0));
}

// Validate 4->2 channel mapping.
// Expectation: 4-chan source is "4 corners" FL.FR.BL.BR (or other L.R.L.R geometry).
// We assign equal weight to the source channels on each side.
// dest[0] is average of source[0] and [2]; dest[1] is average of source[1] and [3].
TEST(SrcReaderTest, Map_4_2) {
  using SR = mixer::SrcReader<float, 4, 2>;
  const float data[] = {-0.25, 0.75, 1.0, -0.5, 0.0};
  const float expect[] = {0.375, 0.125, 0.5};

  EXPECT_EQ(SR::Read(data, 0), expect[0]);
  EXPECT_EQ(SR::Read(data, 1), expect[1]);
  EXPECT_EQ(SR::Read(data + 1, 0), expect[1]);
  EXPECT_EQ(SR::Read(data + 1, 1), expect[2]);
}

// No built-in 4->3 mapping is provided

// No built-in mappings are provided for configs with source channels or dest channels above 4
// (other than the "pass-thru" "N->N and "unity" 1->N mappings).

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
