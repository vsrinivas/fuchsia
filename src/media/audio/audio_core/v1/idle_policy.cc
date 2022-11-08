// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/idle_policy.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <unordered_set>

#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v1/active_stream_count_reporter.h"
#include "src/media/audio/audio_core/v1/context.h"
#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/audio_core/v1/route_graph.h"

namespace media::audio {

std::optional<zx::duration> IdlePolicy::idle_countdown_duration_;
std::optional<zx::duration> IdlePolicy::startup_idle_countdown_duration_;
bool IdlePolicy::use_all_ultrasonic_channels_ = true;

// AudioAdmin::ActiveStreamCountReporter implementation
//
// Will be called on the FIDL thread
void IdlePolicy::OnActiveRenderCountChanged(RenderUsage usage, uint32_t count) {
  if (!IdlePolicy::idle_countdown_duration().has_value() || IdlePolicy::kDisableIdlePolicy) {
    if constexpr (kLogIdlePolicyCounts) {
      FX_LOGS(INFO) << __FUNCTION__ << " exiting early (idle policy disabled)";
    }
    return;
  }
  if constexpr (kLogIdlePolicyCounts) {
    FX_LOGS(INFO) << __FUNCTION__ << "(" << RenderUsageToString(usage) << ", " << count << ")";
  }

  const RoutingScope scope = (usage == RenderUsage::ULTRASOUND ? kUltrasonicOnly : kAudibleOnly);

  std::lock_guard<std::mutex> lock(idle_state_mutex_);
  PrepareForRoutingChange(/* is_input = */ false, scope);

  if (count) {
    active_render_usages_.insert(StreamUsage::WithRenderUsage(usage));
  } else {
    active_render_usages_.erase(StreamUsage::WithRenderUsage(usage));
  }

  DigestRoutingChange(/* is_input = */ false, scope);
}

// DeviceRouter implementation
//
// Will be called on the FIDL thread
void IdlePolicy::AddDeviceToRoutes(AudioDevice* device) {
  if constexpr (kLogIdlePolicyCounts) {
    FX_LOGS(INFO) << __FUNCTION__ << "(" << device << ")";
  }

  std::lock_guard<std::mutex> lock(idle_state_mutex_);
  PrepareForRoutingChange(device->is_input(), kAudibleAndUltrasonic);

  context_->route_graph().AddDeviceToRoutes(device);

  DigestRoutingChange(device->is_input(), kAudibleAndUltrasonic);
}

// Will be called on the FIDL thread
void IdlePolicy::RemoveDeviceFromRoutes(AudioDevice* device) {
  if constexpr (kLogIdlePolicyCounts) {
    FX_LOGS(INFO) << __FUNCTION__ << "(" << device << ")";
  }

  std::lock_guard<std::mutex> lock(idle_state_mutex_);
  PrepareForRoutingChange(device->is_input(), kAudibleAndUltrasonic);

  context_->route_graph().RemoveDeviceFromRoutes(device);

  DigestRoutingChange(device->is_input(), kAudibleAndUltrasonic);
}

// Will be called on the FIDL thread
void IdlePolicy::PrepareForRoutingChange(bool device_is_input, RoutingScope scope) {
  if (device_is_input) {
    return;
  }

  if (!IdlePolicy::idle_countdown_duration().has_value() || IdlePolicy::kDisableIdlePolicy) {
    if constexpr (kLogIdlePolicyCounts) {
      FX_LOGS(INFO) << __FUNCTION__ << ": not caching routing state (idle policy disabled)";
    }
    return;
  }

  if constexpr (kLogIdlePolicyCounts) {
    FX_LOGS(INFO) << __FUNCTION__ << ": caching state before RouteGraph::AddDeviceToRoutes";
  }

  if (scope == kAudibleOnly || scope == kAudibleAndUltrasonic) {
    audible_devices_before_device_change_ = ActiveDevices(/* ultrasonic_only = */ false);
  }

  if (scope == kUltrasonicOnly || scope == kAudibleAndUltrasonic) {
    ultrasonic_devices_before_device_change_ = ActiveDevices(/* ultrasonic_only = */ true);
  }
}

// Will be called on the FIDL thread
void IdlePolicy::DigestRoutingChange(bool device_is_input, RoutingScope scope) {
  if (device_is_input) {
    return;
  }

  if (!IdlePolicy::idle_countdown_duration().has_value() || IdlePolicy::kDisableIdlePolicy) {
    if constexpr (kLogIdlePolicyCounts) {
      FX_LOGS(INFO) << __FUNCTION__ << ": not changing active channels (idle policy disabled)";
    }
    return;
  }

  if (scope == kAudibleOnly || scope == kAudibleAndUltrasonic) {
    auto audible_devices_after = ActiveDevices(/* ultrasonic_only = */ false);

    // First take care of devices that were - but are no longer - routed to an active RenderUsage
    for (auto& dev : audible_devices_before_device_change_) {
      if (audible_devices_after.count(dev)) {
        audible_devices_after.erase(dev);  // still active after, so remove it from our attention
        continue;
      }
      if constexpr (kLogIdlePolicyCounts) {
        FX_LOGS(INFO) << __FUNCTION__ << " calling StartCountdownToDisableAudible";
      }
      if (IdlePolicy::idle_countdown_duration().has_value()) {
        dev->StartCountdownToDisableAudible(*IdlePolicy::idle_countdown_duration());
      }
    }
    audible_devices_before_device_change_.clear();

    // Only devices remaining are ones that are newly targeted by an active RenderUsage
    for (auto& dev : audible_devices_after) {
      if constexpr (kLogIdlePolicyCounts) {
        FX_LOGS(INFO) << __FUNCTION__ << " calling EnableAudible";
      }
      dev->EnableAudible();
    }
  }

  if (scope == kUltrasonicOnly || scope == kAudibleAndUltrasonic) {
    auto ultrasonic_devices_after = ActiveDevices(/* ultrasonic_only = */ true);

    for (auto& dev : ultrasonic_devices_before_device_change_) {
      if (ultrasonic_devices_after.count(dev)) {
        ultrasonic_devices_after.erase(dev);
        continue;
      }
      if constexpr (kLogIdlePolicyCounts) {
        FX_LOGS(INFO) << __FUNCTION__ << " calling StartCountdownToDisableUltrasonic";
      }
      if (IdlePolicy::idle_countdown_duration().has_value()) {
        dev->StartCountdownToDisableUltrasonic(*IdlePolicy::idle_countdown_duration());
      }
    }

    ultrasonic_devices_before_device_change_.clear();

    for (auto& dev : ultrasonic_devices_after) {
      if constexpr (kLogIdlePolicyCounts) {
        FX_LOGS(INFO) << __FUNCTION__ << " calling EnableUltrasonic";
      }
      dev->EnableUltrasonic();
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
