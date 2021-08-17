// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_IDLE_POLICY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_IDLE_POLICY_H_

#include <unordered_set>

#include "lib/zx/time.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/device_registry.h"
#include "src/media/audio/audio_core/stream_usage.h"

namespace media::audio {

class AudioDevice;

class IdlePolicy : public AudioAdmin::ActiveStreamCountReporter, public DeviceRouter {
 public:
  // To save power, disable outputs when there are no active streams flowing to them.
  static constexpr bool kDisableOutputChannelsOnIdle = true;
  // When a device is added, immediately start an idle countdown in case it isn't used.
  static constexpr bool kSetInitialIdleCountdownWhenConfigured = true;
  // If multiple output channels are ultrasound-capable, only the first one need be enabled.
  static constexpr bool kOnlyEnableFirstUltrasonicChannel = true;
  static_assert(kDisableOutputChannelsOnIdle || !kSetInitialIdleCountdownWhenConfigured,
                "Cannot set kSetInitialIdleCountdownWhenConfigured without "
                "kDisableOutputChannelsOnIdle (device would be incapable of reawakening)");

  // TODO(fxbug.dev/82408): extract these two values to a policy layer or static config.
  // Wait for a period of inactivity before disabling (slow to disable; fast to reenable).
  static constexpr zx::duration kOutputChannelsIdleCountdown = zx::sec(5);
  // By default, power-down outputs if they aren't used in the first 2 minutes.
  static constexpr zx::duration kInitialIdleCountdown = zx::min(2);

  // Informational logging for various aspects of the idle power-conservation mechanism
  static constexpr bool kLogChannelFrequencyRanges = false;
  static constexpr bool kLogIdlePolicyCounts = false;
  static constexpr bool kLogIdleTimers = false;
  static constexpr bool kLogSetActiveChannelsSupport = false;
  static constexpr bool kLogSetActiveChannelsCalls = true;

  explicit IdlePolicy(Context* context = nullptr) : ActiveStreamCountReporter(), context_(context) {
    active_render_usages_.clear();
  }

  // AudioAdmin::ActiveStreamCountReporter implementation
  void OnActiveRenderCountChanged(RenderUsage usage, uint32_t count) final
      FXL_LOCKS_EXCLUDED(idle_state_mutex_);

  // DeviceRouter implementation
  void AddDeviceToRoutes(AudioDevice* device) final FXL_LOCKS_EXCLUDED(idle_state_mutex_);
  void RemoveDeviceFromRoutes(AudioDevice* device) final FXL_LOCKS_EXCLUDED(idle_state_mutex_);

 private:
  enum RoutingScope {
    kAudibleOnly = 1,
    kUltrasonicOnly = 2,
    kAudibleAndUltrasonic = 3,
  };

  void PrepareForRoutingChange(bool device_is_input, RoutingScope scope)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(idle_state_mutex_);
  void DigestRoutingChange(bool device_is_input, RoutingScope scope)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(idle_state_mutex_);

  std::unordered_set<AudioDevice*> ActiveDevices(bool ultrasonic_only)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(idle_state_mutex_);

  // This defends power state changes and timer cancellations/completions.
  std::mutex idle_state_mutex_;

  Context* context_;

  StreamUsageMask active_render_usages_ FXL_GUARDED_BY(idle_state_mutex_);
  std::unordered_set<AudioDevice*> ultrasonic_devices_before_device_change_
      FXL_GUARDED_BY(idle_state_mutex_);
  std::unordered_set<AudioDevice*> audible_devices_before_device_change_
      FXL_GUARDED_BY(idle_state_mutex_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_IDLE_POLICY_H_
