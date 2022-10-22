// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIX_JOB_CONTEXT_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIX_JOB_CONTEXT_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/zx/time.h>

#include <string>

#include <fbl/static_vector.h>

#include "src/media/audio/lib/clock/clock_snapshot.h"
#include "src/media/audio/services/mixer/mix/mix_job_subtask.h"

namespace media_audio {

// MixJobContext provides a container for state that can be carried throughout a mix job.
// This class must not allocate anything on the heap.
class MixJobContext {
 private:
  // Capacity of per_subtask_metrics_.
  static constexpr size_t kMaxSubtasks = 16;

 public:
  MixJobContext(const ClockSnapshots& clocks, zx::time mono_start_time, zx::time mono_deadline);

  // Returns the set of clocks available during this mix job.
  const ClockSnapshots& clocks() const { return clocks_; }

  // Reports the start time of this mix job relative to the given clock.
  zx::time start_time(const UnreadableClock& clock) const;

  // Reports the deadline of this mix job relative to the given clock.
  zx::time deadline(const UnreadableClock& clock) const;

  // Adds metrics for the given subtask. Internally we maintain one Metrics object per subtask. If
  // this method is called multiple times with the same subtask name, the metrics are accumulated.
  void AddSubtaskMetrics(const MixJobSubtask::Metrics& new_subtask) {
    for (auto& old_subtask : per_subtask_metrics_) {
      if (std::string_view{old_subtask.name} == std::string_view{new_subtask.name}) {
        old_subtask += new_subtask;
        return;
      }
    }
    // Add a new subtask, or silently drop if we've exceeded the maximum.
    if (per_subtask_metrics_.size() < kMaxSubtasks) {
      per_subtask_metrics_.push_back(new_subtask);
    }
  }

  // Returns all metrics accumulated via AddMetrics.
  using SubtaskMetricsVector = fbl::static_vector<MixJobSubtask::Metrics, kMaxSubtasks>;
  const SubtaskMetricsVector& per_subtask_metrics() { return per_subtask_metrics_; }

 private:
  const ClockSnapshots& clocks_;
  zx::time mono_start_time_;
  zx::time mono_deadline_;
  zx::duration mix_period_;
  SubtaskMetricsVector per_subtask_metrics_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIX_JOB_CONTEXT_H_
