// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/mixer/gain.h"

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

class AudioLinkChild : public AudioLink {
 public:
  AudioLinkChild(SourceType source_type, fbl::RefPtr<AudioObject> source,
                 fbl::RefPtr<AudioObject> dest)
      : AudioLink(source_type, source, dest) {}
};

TEST(AudioLinkTest, LinkObjectWithVolumeCurve) {
  const auto no_curve = fbl::AdoptRef(new MockObjectNoCurve());
  const auto with_curve =
      fbl::AdoptRef(new MockObjectWithCurve(VolumeCurve::DefaultForMinGain(-10.0)));

  AudioLinkChild link(AudioLink::SourceType::Packet, no_curve, with_curve);

  // Here we ensure our provided curve is loaded, and not the fallback.
  EXPECT_FLOAT_EQ(link.volume_curve().VolumeToDb(0.5), -5.0);
}

TEST(AudioLinkTest, LinkObjectsWithNoCurves) {
  const auto no_curve1 = fbl::AdoptRef(new MockObjectNoCurve());
  const auto no_curve2 = fbl::AdoptRef(new MockObjectNoCurve());

  AudioLinkChild link(AudioLink::SourceType::Packet, no_curve1, no_curve2);
  // This passes by not crashing, to ensure we accept this valid case.

  // A reasonable fallback curve should be loaded.
  EXPECT_FLOAT_EQ(link.volume_curve().VolumeToDb(fuchsia::media::audio::MIN_VOLUME),
                  fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(link.volume_curve().VolumeToDb(fuchsia::media::audio::MAX_VOLUME),
                  Gain::kUnityGainDb);
}

}  // namespace
}  // namespace media::audio
