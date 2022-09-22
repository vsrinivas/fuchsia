// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GAIN_CONTROL_REGISTRY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GAIN_CONTROL_REGISTRY_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <unordered_map>

#include "src/media/audio/lib/clock/clock_snapshot.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"

namespace media_audio {

// Class that contains the set of all gain controls used by a single mix graph.
//
// This class is not safe for concurrent use.
class GainControlRegistry {
 public:
  // Adds new gain control with the given `gain_id` and `reference_clock`.
  //
  // Required: A `GainControl` must not exist already with this `gain_id`.
  void Add(GainControlId gain_id, UnreadableClock reference_clock);

  // Returns the gain control with the given `gain_id`.
  //
  // Required: A `GainControl` must exist with this `gain_id`.
  GainControl& Get(GainControlId gain_id);
  const GainControl& Get(GainControlId gain_id) const;

  // Removes the gain control with the given `gain_id`.
  //
  // Required: A `GainControl` must exist with this `gain_id`.
  void Remove(GainControlId gain_id);

  // Advances all gain controls at once to a given `mono_time`.
  void Advance(const ClockSnapshots& clocks, zx::time mono_time);

 private:
  std::unordered_map<GainControlId, GainControl> gain_controls_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_GAIN_CONTROL_REGISTRY_H_
