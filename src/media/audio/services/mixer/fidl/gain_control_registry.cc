// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/gain_control_registry.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"

namespace media_audio {

void GainControlRegistry::Add(GainControlId gain_id, UnreadableClock reference_clock) {
  FX_CHECK(gain_controls_.emplace(gain_id, GainControl(reference_clock)).second);
}

GainControl& GainControlRegistry::Get(GainControlId gain_id) {
  const auto it = gain_controls_.find(gain_id);
  FX_CHECK(it != gain_controls_.end());
  return it->second;
}

const GainControl& GainControlRegistry::Get(GainControlId gain_id) const {
  const auto it = gain_controls_.find(gain_id);
  FX_CHECK(it != gain_controls_.end());
  return it->second;
}

void GainControlRegistry::Remove(GainControlId gain_id) {
  FX_CHECK(gain_controls_.erase(gain_id) > 0);
}

void GainControlRegistry::Advance(const ClockSnapshots& clocks, zx::time mono_time) {
  for (auto& [id, gain_control] : gain_controls_) {
    const auto clock = clocks.SnapshotFor(gain_control.reference_clock());
    gain_control.Advance(clock.ReferenceTimeFromMonotonicTime(mono_time));
  }
}

}  // namespace media_audio
