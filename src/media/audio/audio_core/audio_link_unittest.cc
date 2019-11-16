// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/testing/matchers.h"

using ::media::audio::testing::VolumeMappingEq;
using ::testing::Pointwise;

namespace media::audio {
namespace {

class MockObjectNoCurve : public AudioObject {
 public:
  MockObjectNoCurve() : AudioObject(AudioObject::Type::Output) {}
};

class MockObjectWithCurve : public AudioObject {
 public:
  MockObjectWithCurve(VolumeCurve volume_curve)
      : AudioObject(AudioObject::Type::Output), volume_curve_(std::move(volume_curve)) {}

  std::optional<VolumeCurve> GetVolumeCurve() const override { return {volume_curve_}; }

 private:
  VolumeCurve volume_curve_;
};

TEST(AudioLinkTest, LinkObjectWithVolumeCurve) {
  const auto no_curve = fbl::AdoptRef(new MockObjectNoCurve());
  const auto with_curve =
      fbl::AdoptRef(new MockObjectWithCurve(VolumeCurve::DefaultForMinGain(-10.0)));

  AudioLink link(no_curve, with_curve);

  // Here we ensure our provided curve is loaded, and not the fallback.
  EXPECT_FLOAT_EQ(link.volume_curve().VolumeToDb(0.5), -5.0);
}

TEST(AudioLinkTest, LinkObjectsWithNoCurves) {
  const auto no_curve1 = fbl::AdoptRef(new MockObjectNoCurve());
  const auto no_curve2 = fbl::AdoptRef(new MockObjectNoCurve());
  auto default_curve = VolumeCurve::DefaultForMinGain(-33.0);
  auto process_config = ProcessConfig::Builder().SetDefaultVolumeCurve(default_curve).Build();
  auto config_handle = ProcessConfig::set_instance(process_config);

  AudioLink link(no_curve1, no_curve2);
  // This passes by not crashing, to ensure we accept this valid case.

  // The default curve should be provided.
  EXPECT_THAT(link.volume_curve().mappings(),
              Pointwise(VolumeMappingEq(), default_curve.mappings()));
}

}  // namespace
}  // namespace media::audio
