// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_IDLE_POLICY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_IDLE_POLICY_H_

#include <unordered_set>

#include "lib/zx/time.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_admin.h"

namespace media::audio {

class AudioDevice;

class IdlePolicy : public AudioAdmin::ActiveStreamCountReporter {
 public:
  static constexpr bool kOnlyUseFirstUltrasonicChannel = true;

  // TODO(fxbug.dev/82408): extract these two values to a policy layer or static config.
  // By default, power-down outputs if they aren't used in the first 2 minutes.
  // After proof-of-concept, this should likely be removed, to keep AudioCore mechanism-only.
  static constexpr zx::duration kInitialPowerDownDelay = zx::min(2);

  // Wait for a period of inactivity, to be "slow to disable but fast to reenable".
  // To be mechanism-only, we should move this value from code to a product-specific configuration.
  static constexpr zx::duration kOutputIdlePowerDownTimeout = zx::sec(5);

  static constexpr bool kDebugChannelFrequencyRangeIteration = false;
  static constexpr bool kDebugActivityCounts = false;
  static constexpr bool kDebugIdleTimers = false;
  static constexpr bool kDebugSetActiveChannelsLogic = false;
  static constexpr bool kLogSetActiveChannelsCalls = true;

  explicit IdlePolicy(Context* context = nullptr) : ActiveStreamCountReporter(), context_(context) {
    active_render_usages_.clear();
  }

  void OnActiveRenderCountChanged(RenderUsage usage, uint32_t count) final
      FXL_LOCKS_EXCLUDED(idle_state_mutex_);

 private:
  // This defends power state changes and timer cancellations/completions.
  std::mutex idle_state_mutex_;

  std::unordered_set<AudioDevice*> ActiveDevices(bool ultrasonic_only)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(idle_state_mutex_);

  Context* context_;

  StreamUsageMask active_render_usages_ FXL_GUARDED_BY(idle_state_mutex_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_IDLE_POLICY_H_
