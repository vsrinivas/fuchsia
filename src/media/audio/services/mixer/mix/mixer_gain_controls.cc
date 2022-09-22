// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mixer_gain_controls.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <unordered_set>
#include <utility>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"

namespace media_audio {

void MixerGainControls::Add(GainControlId gain_id, GainControl gain_control) {
  FX_CHECK(gain_controls_.emplace(gain_id, std::move(gain_control)).second);
}

void MixerGainControls::Remove(GainControlId gain_id) {
  FX_CHECK(gain_controls_.erase(gain_id) > 0);
}

GainControl& MixerGainControls::Get(GainControlId gain_id) {
  const auto it = gain_controls_.find(gain_id);
  FX_CHECK(it != gain_controls_.end());
  return it->second;
}

const GainControl& MixerGainControls::Get(GainControlId gain_id) const {
  const auto it = gain_controls_.find(gain_id);
  FX_CHECK(it != gain_controls_.end());
  return it->second;
}

void MixerGainControls::Advance(const ClockSnapshots& clocks, zx::time mono_time) {
  for (auto& [gain_id, gain_control] : gain_controls_) {
    const auto clock = clocks.SnapshotFor(gain_control.reference_clock());
    gain_control.Advance(clock.ReferenceTimeFromMonotonicTime(mono_time));
  }
}

std::optional<zx::time> MixerGainControls::NextScheduledStateChange(
    const ClockSnapshots& clocks) const {
  std::optional<zx::time> min_next_mono_time = std::nullopt;
  for (auto& [gain_id, gain_control] : gain_controls_) {
    const auto next_reference_time = gain_control.NextScheduledStateChange();
    if (!next_reference_time.has_value()) {
      continue;
    }
    const auto clock = clocks.SnapshotFor(gain_control.reference_clock());
    if (const auto next_mono_time = clock.MonotonicTimeFromReferenceTime(*next_reference_time);
        !min_next_mono_time.has_value() || next_mono_time < *min_next_mono_time) {
      min_next_mono_time = next_mono_time;
    }
  }
  return min_next_mono_time;
}

}  // namespace media_audio
