// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_GAIN_CONTROLS_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_GAIN_CONTROLS_H_

#include <lib/zx/time.h>

#include <optional>
#include <unordered_map>
#include <utility>

#include "src/media/audio/lib/clock/clock_snapshot.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"

namespace media_audio {

// Class that contains the set of all gain controls used by a single `MixerStage`.
//
// This class is not safe for concurrent use.
class MixerGainControls {
 public:
  // Adds `gain_control` with the given `gain_id`.
  //
  // Required: A `GainControl` must not exist already with this `gain_id`.
  void Add(GainControlId gain_id, GainControl gain_control);

  // Removes the gain control with the given `gain_id`.
  //
  // Required: A `GainControl` must exist with this `gain_id`.
  void Remove(GainControlId gain_id);

  // Returns the gain control with the given `gain_id`.
  //
  // Required: A `GainControl` must exist with this `gain_id`.
  GainControl& Get(GainControlId gain_id);
  const GainControl& Get(GainControlId gain_id) const;

  // Advances all gain controls at once to a given `mono_time`.
  void Advance(const ClockSnapshots& clocks, zx::time mono_time);

  // Returns the next scheduled state change monotonic time amongst all gain controls, or nullopt if
  // no changes are scheduled.
  std::optional<zx::time> NextScheduledStateChange(const ClockSnapshots& clocks) const;

 private:
  std::unordered_map<GainControlId, GainControl> gain_controls_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_GAIN_CONTROLS_H_
