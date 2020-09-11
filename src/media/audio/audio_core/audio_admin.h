// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_ADMIN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_ADMIN_H_

#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_set>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/fxl/synchronization/thread_checker.h"
#include "src/media/audio/audio_core/policy_loader.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"

namespace media {
namespace audio {

class AudioAdmin {
 public:
  struct BehaviorGain {
    float none_gain_db;
    float duck_gain_db;
    float mute_gain_db;
  };

  // An interface by which |AudioAdmin| can report actions taken on usages. Policy is reactive, so
  // any time a usage's active status (active: at least one stream is active on the usage, inactive:
  // no streams are active on the usage) changes, all usages will be notified of the policy action
  // taken on them.
  class PolicyActionReporter {
   public:
    virtual void ReportPolicyAction(fuchsia::media::Usage usage,
                                    fuchsia::media::Behavior policy_action) = 0;
  };

  // An interface by which |AudioAdmin| can report which AudioRenderUsages are active.
  class ActivityDispatcher {
   public:
    virtual ~ActivityDispatcher() {}

    using RenderActivity = std::bitset<fuchsia::media::RENDER_USAGE_COUNT>;
    virtual void OnRenderActivityChanged(
        std::bitset<fuchsia::media::RENDER_USAGE_COUNT> activity) = 0;
  };

  // Constructs an |AudioAdmin| from a |BehaviorGain| and |GainAdjustment|.
  //
  // The |BehaviorGain| provides the target gain_db values to use when triggering behaviors between
  // usages, simply mapping each behavior to a relative gain value. The |GainAdjustment| is simply
  // an interface that this object will use to apply the target gain values in |BehaviorGain|.
  //
  // |gain_adjustment| must be non-null.
  AudioAdmin(BehaviorGain behavior_gain, StreamVolumeManager* volume_manager,
             PolicyActionReporter* policy_action_reporter, ActivityDispatcher* activity_dispatcher,
             async_dispatcher_t* fidl_dispatcher);

  // Constructs an |AudioAdmin| using some default |BehaviorGain| values.
  AudioAdmin(StreamVolumeManager* volume_manager, async_dispatcher_t* fidl_dispatcher,
             PolicyActionReporter* policy_action_reporter, ActivityDispatcher* activity_dispatcher);

  // Sets the interaction behavior between |active| and |affected| usages.
  void SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                      fuchsia::media::Behavior behavior);

  // Clears all configured behaviors.
  void ResetInteractions() {
    std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
    active_rules_.ResetInteractions();
  };

  // Clears all configured behaviors and then applies the rules in the provided AudioPolicy.
  void SetInteractionsFromAudioPolicy(AudioPolicy policy);

  // Interface used by AudiolCoreImpl for accounting
  void UpdateRendererState(fuchsia::media::AudioRenderUsage usage, bool active,
                           fuchsia::media::AudioRenderer* renderer);
  void UpdateCapturerState(fuchsia::media::AudioCaptureUsage usage, bool active,
                           fuchsia::media::AudioCapturer* capturer);

  bool IsActive(fuchsia::media::AudioRenderUsage usage);
  bool IsActive(fuchsia::media::AudioCaptureUsage usage);

 private:
  const BehaviorGain behavior_gain_;
  StreamVolumeManager& stream_volume_manager_ FXL_GUARDED_BY(fidl_thread_checker_);
  PolicyActionReporter& policy_action_reporter_ FXL_GUARDED_BY(fidl_thread_checker_);
  ActivityDispatcher& activity_dispatcher_ FXL_GUARDED_BY(fidl_thread_checker_);

  // Ensures we are always on the thread on which the class was constructed, which should
  // be the FIDL thread.
  fxl::ThreadChecker fidl_thread_checker_;
  async_dispatcher_t* fidl_dispatcher_;

  void UpdatePolicy();
  void UpdateRenderActivity();

  // Helpers to make the control of streams cleaner.
  void SetUsageNone(fuchsia::media::AudioRenderUsage usage);
  void SetUsageNone(fuchsia::media::AudioCaptureUsage usage);

  void SetUsageMute(fuchsia::media::AudioRenderUsage usage);
  void SetUsageMute(fuchsia::media::AudioCaptureUsage usage);

  void SetUsageDuck(fuchsia::media::AudioRenderUsage usage);
  void SetUsageDuck(fuchsia::media::AudioCaptureUsage usage);

  bool IsUsageMuted(fuchsia::media::AudioRenderUsage usage);
  bool IsUsageMuted(fuchsia::media::AudioCaptureUsage usage);

  bool IsUsageDucked(fuchsia::media::AudioRenderUsage usage);
  bool IsUsageDucked(fuchsia::media::AudioCaptureUsage usage);

  void InitPolicies();

  void ApplyPolicies(fuchsia::media::AudioCaptureUsage category);
  void ApplyPolicies(fuchsia::media::AudioRenderUsage category);

  class PolicyRules {
   public:
    int ToIndex(fuchsia::media::AudioCaptureUsage usage) {
      return fidl::ToUnderlying(usage) + fuchsia::media::RENDER_USAGE_COUNT;
    }

    int ToIndex(fuchsia::media::AudioRenderUsage usage) { return fidl::ToUnderlying(usage); }

    template <typename T, typename U>
    void SetRule(T source, U target, fuchsia::media::Behavior policy) {
      active_affected_[ToIndex(source)][ToIndex(target)] = policy;
    }

    template <typename T, typename U>
    fuchsia::media::Behavior GetPolicy(T source, U target) {
      return active_affected_[ToIndex(source)][ToIndex(target)];
    }

    void ResetInteractions();

    PolicyRules() { ResetInteractions(); }

   private:
    fuchsia::media::Behavior
        active_affected_[fuchsia::media::RENDER_USAGE_COUNT + fuchsia::media::CAPTURE_USAGE_COUNT]
                        [fuchsia::media::RENDER_USAGE_COUNT + fuchsia::media::CAPTURE_USAGE_COUNT];
  };
  PolicyRules active_rules_ FXL_GUARDED_BY(fidl_thread_checker_);

  std::unordered_set<fuchsia::media::AudioRenderer*>
      active_streams_playback_[fuchsia::media::RENDER_USAGE_COUNT] FXL_GUARDED_BY(
          fidl_thread_checker_);
  std::unordered_set<fuchsia::media::AudioCapturer*>
      active_streams_capture_[fuchsia::media::CAPTURE_USAGE_COUNT] FXL_GUARDED_BY(
          fidl_thread_checker_);
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_ADMIN_H_
