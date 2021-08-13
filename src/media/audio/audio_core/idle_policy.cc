// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/idle_policy.h"

#include <lib/syslog/cpp/macros.h>

#include <unordered_set>

#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_usage.h"

namespace media::audio {

// Will be called on the FIDL thread
void IdlePolicy::OnActiveRenderCountChanged(RenderUsage usage, uint32_t count) {
  std::lock_guard<std::mutex> lock(idle_state_mutex_);

  // Get active devices before (ultrasonic or audible)
  std::unordered_set<AudioDevice*> active_devices_before =
      ActiveDevices(usage == RenderUsage::ULTRASOUND);

  if (count) {
    active_render_usages_.insert(StreamUsage::WithRenderUsage(usage));
  } else {
    active_render_usages_.erase(StreamUsage::WithRenderUsage(usage));
  }

  // Get active devices after (ultrasonic or audible)
  std::unordered_set<AudioDevice*> active_devices_after =
      ActiveDevices(usage == RenderUsage::ULTRASOUND);

  if constexpr (IdlePolicy::kDebugActivityCounts) {
    FX_LOGS(INFO) << __FUNCTION__ << "(" << RenderUsageToString(usage) << ", " << count
                  << ") -- previous active_device was " << active_devices_before.size()
                  << " and is now " << active_devices_after.size();
  }

  for (auto& dev : active_devices_before) {
    if (active_devices_after.count(dev)) {
      continue;  // is still active after
    }

    // this device's active-renderer count just became 0: start a cancellable countdown to disable
    zx_status_t status =
        (usage == RenderUsage::ULTRASOUND)
            ? dev->StartCountdownToDisableUltrasonic(IdlePolicy::kOutputIdlePowerDownTimeout)
            : dev->StartCountdownToDisableAudible(IdlePolicy::kOutputIdlePowerDownTimeout);

    if constexpr (IdlePolicy::kDebugActivityCounts) {
      FX_LOGS(INFO) << __FUNCTION__ << "(" << RenderUsageToString(usage) << ", " << count
                    << "): device->Countdown("
                    << IdlePolicy::kOutputIdlePowerDownTimeout.get() / ZX_MSEC(1) << "ms) returned "
                    << status;
    }
  }

  for (auto& dev : active_devices_after) {
    if (active_devices_before.count(dev)) {
      continue;  // was also active before
    }

    // this device's active-renderer count just changed from 0 to >0: enable it
    zx_status_t status =
        (usage == RenderUsage::ULTRASOUND) ? dev->EnableUltrasonic() : dev->EnableAudible();

    if constexpr (IdlePolicy::kDebugActivityCounts) {
      FX_LOGS(INFO) << __FUNCTION__ << "(" << RenderUsageToString(usage) << ", " << count
                    << "): device->Enable returned " << status;
    }
  }
}

std::unordered_set<AudioDevice*> IdlePolicy::ActiveDevices(bool ultrasonic_only) {
  std::unordered_set<AudioDevice*> active_devices;

  for (const auto& usage : kRenderUsages) {
    if (ultrasonic_only == (usage == RenderUsage::ULTRASOUND)) {
      if (active_render_usages_.contains(StreamUsage::WithRenderUsage(usage))) {
        auto outputs = context_->route_graph().TargetsForRenderUsage(usage);
        active_devices.insert(outputs.begin(), outputs.end());
      }
    }
  }

  return active_devices;
}

}  // namespace media::audio
