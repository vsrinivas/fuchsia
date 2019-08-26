// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_GAIN_CURVE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_GAIN_CURVE_H_

#include <lib/fit/result.h>

#include <optional>
#include <vector>

namespace media::audio {

// A gain curve is a continuous increasing piecewise linear function that maps from volume over the
// domain [0.0, 1.0] to gain in dbfs.
class GainCurve {
 public:
  enum Error {
    kLessThanTwoMappingsCannotMakeCurve,
    kDomain0to1NotCovered,
    kNonIncreasingDomainIllegal,
    kNonIncreasingRangeIllegal,
    kRange0NotCovered,
  };

  // A mapping from volume domain to gain in dbfs.
  struct VolumeMapping {
    VolumeMapping(float volume_in, float gain_dbfs_in)
        : volume(volume_in), gain_dbfs(gain_dbfs_in) {}

    float volume;
    float gain_dbfs;
  };

  // Attempts to construct a curve from a mapping from volume domain to gain in dbfs. Mappings must
  // represent a continuous increasing function from volume to gain in dbfs over the volume domain
  // [0.0, 1.0]. The gain range must start with a negative value and end exactly at 0.0.
  static fit::result<GainCurve, Error> FromMappings(std::vector<VolumeMapping> mappings);

  // Samples the gain curve for the dbfs value at `volume`. Outside of [0.0, 1.0], the volume is
  // clamped before sampling.
  float VolumeToDb(float volume) const;

 private:
  explicit GainCurve(std::vector<VolumeMapping> mappings);

  // Returns the bounds, the neighboring mappings to volume x. If x is 0.5, and we have mappings at
  // volumes [0.0, 0.25, 0.75, 1.0] the mappings at 0.25 and 0.75 will be returned as bounds. If two
  // bounds do not exist, std::nullopt is returned. Mappings may be equal to x on one side.
  std::optional<std::pair<VolumeMapping, VolumeMapping>> Bounds(float x) const;

  // Mappings stored with the assumptions that 1) the map is sorted by volume, 2) there are
  // at least two mappings, 3) the volume domain includes [0.0, 1.0], and 4) the final mapping is
  // 1.0 => 0.0 dbfs.
  std::vector<VolumeMapping> mappings_ = {};
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_GAIN_CURVE_H_
