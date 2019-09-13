// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_object.h"

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
  const auto with_curve = fbl::AdoptRef(new MockObjectWithCurve(VolumeCurve::Default()));

  AudioLinkChild link(AudioLink::SourceType::Packet, no_curve, with_curve);

  EXPECT_TRUE(link.volume_curve().has_value());
}

TEST(AudioLinkTest, LinkObjectsWithNoCurves) {
  const auto no_curve1 = fbl::AdoptRef(new MockObjectNoCurve());
  const auto no_curve2 = fbl::AdoptRef(new MockObjectNoCurve());

  AudioLinkChild link(AudioLink::SourceType::Packet, no_curve1, no_curve2);
  // This passes by not crashing, to ensure we accept this valid case.

  EXPECT_FALSE(link.volume_curve().has_value());
}

}  // namespace
}  // namespace media::audio
