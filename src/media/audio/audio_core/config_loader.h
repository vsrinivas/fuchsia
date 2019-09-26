// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CONFIG_LOADER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CONFIG_LOADER_H_

#include <optional>

#include "src/media/audio/audio_core/volume_curve.h"

namespace media::audio {

class ConfigLoader {
 public:
  // Loads a volume curve from disk, defined according to volume_curve_schema.jsx. The curve is
  // expected to be correct and defined at build time. This will panic if the curve file is invalid.
  //
  // Returns the curve if the file was present, or std::nullopt if the file was not present.
  static std::optional<VolumeCurve> LoadVolumeCurveFromDisk(const char* filename);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CONFIG_LOADER_H_
