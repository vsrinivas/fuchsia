// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_IDLE_POLICY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_IDLE_POLICY_H_

#include <lib/zx/time.h>

#include <unordered_set>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/shared/active_stream_count_reporter.h"
#include "src/media/audio/audio_core/shared/audio_admin.h"
#include "src/media/audio/audio_core/shared/audio_policy.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v1/context.h"
#include "src/media/audio/audio_core/v1/device_registry.h"
#include "src/media/audio/audio_core/v1/logging_flags.h"

namespace media::audio {

class AudioDevice;

class IdlePolicy : public ActiveStreamCountReporter, public DeviceRouter {
 public:
  static constexpr bool kDisableIdlePolicy = false;

  explicit IdlePolicy(Context* context = nullptr) : ActiveStreamCountReporter(), context_(context) {
    active_render_usages_.clear();
  }

  // AudioAdmin::ActiveStreamCountReporter implementation
  void OnActiveRenderCountChanged(RenderUsage usage, uint32_t count) final
      FXL_LOCKS_EXCLUDED(idle_state_mutex_);

  // DeviceRouter implementation
  void AddDeviceToRoutes(AudioDevice* device) final FXL_LOCKS_EXCLUDED(idle_state_mutex_);
  void RemoveDeviceFromRoutes(AudioDevice* device) final FXL_LOCKS_EXCLUDED(idle_state_mutex_);
  void SetIdlePowerOptionsFromPolicy(AudioPolicy::IdlePowerOptions options) override {
    idle_countdown_duration_ = options.idle_countdown_duration;
    startup_idle_countdown_duration_ = options.startup_idle_countdown_duration;
    use_all_ultrasonic_channels_ = options.use_all_ultrasonic_channels;
    if constexpr (kLogIdlePolicyStaticConfigValues) {
      FX_LOGS(INFO) << "idle_countdown_duration: "
                    << idle_countdown_duration_.value_or(zx::duration(-1)).get()
                    << ", startup_idle_countdown_duration: "
                    << startup_idle_countdown_duration_.value_or(zx::duration(-1)).get()
                    << ", use_all_ultrasonic_channels: " << use_all_ultrasonic_channels_;
    }
  }

  static inline std::optional<zx::duration> idle_countdown_duration() {
    return idle_countdown_duration_;
  }
  static inline std::optional<zx::duration> startup_idle_countdown_duration() {
    return startup_idle_countdown_duration_;
  }
  static inline bool use_all_ultrasonic_channels() { return use_all_ultrasonic_channels_; }

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
  std::unordered_set<AudioDevice*> audible_devices_before_device_change_
      FXL_GUARDED_BY(idle_state_mutex_);
  std::unordered_set<AudioDevice*> ultrasonic_devices_before_device_change_
      FXL_GUARDED_BY(idle_state_mutex_);

  // If this value is nullopt, the entire "power-down idle outputs" feature is disabled.
  static std::optional<zx::duration> idle_countdown_duration_;

  // Outputs are enabled at driver-start. When this value is nullopt, outputs remain enabled and
  // ready indefinitely, until they are targeted by a render stream.
  static std::optional<zx::duration> startup_idle_countdown_duration_;

  // If true, all ultrasonic-capable channels will be enabled/disabled as an intact set.
  // Else, ultrasonic content requires only the FIRST ultrasonic-capable channel to be enabled.
  static bool use_all_ultrasonic_channels_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_IDLE_POLICY_H_
