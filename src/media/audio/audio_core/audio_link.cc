// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link.h"

#include "src/media/audio/audio_core/audio_object.h"

namespace media::audio {
namespace {

// Returns one of two curves, if either of them exist. Otherwise returns std::nullopt;
std::optional<VolumeCurve> SelectVolumeCurve(std::optional<VolumeCurve> curve_a,
                                             std::optional<VolumeCurve> curve_b) {
  FX_DCHECK(!(curve_a.has_value() && curve_b.has_value()))
      << "Two objects with a gain curve cannot be linked.";

  if (curve_a.has_value()) {
    return curve_a;
  }

  return curve_b;
}

}  // namespace

AudioLink::AudioLink(fbl::RefPtr<AudioObject> source, fbl::RefPtr<AudioObject> dest)
    : source_(std::move(source)),
      dest_(std::move(dest)),
      valid_(true),
      volume_curve_(SelectVolumeCurve(source_->GetVolumeCurve(), dest_->GetVolumeCurve())) {
  // Only outputs and AudioCapturers may be destinations.
  FX_DCHECK(dest_ != nullptr);
}

const VolumeCurve& AudioLink::volume_curve() const {
  if (volume_curve_.has_value()) {
    return volume_curve_.value();
  }

  return VolumeCurve::Default();
}

}  // namespace media::audio
