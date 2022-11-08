// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_AUDIO_ADMIN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_AUDIO_ADMIN_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/thread_checker.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_set>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/shared/active_stream_count_reporter.h"
#include "src/media/audio/audio_core/shared/policy_loader.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"

namespace media::audio {

class StreamVolumeManager;

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

    using CaptureActivity = std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT>;
    virtual void OnCaptureActivityChanged(
        std::bitset<fuchsia::media::CAPTURE_USAGE_COUNT> activity) = 0;
  };

  static constexpr BehaviorGain kDefaultGainBehavior = {
      .none_gain_db = 0.0f,
      .duck_gain_db = -35.0f,
      .mute_gain_db = fuchsia::media::audio::MUTED_GAIN_DB,
  };
  // Constructs an |AudioAdmin| from a |BehaviorGain| and |GainAdjustment|.
  //
  // The |BehaviorGain| provides the target gain_db values to use when triggering behaviors between
  // usages, simply mapping each behavior to a relative gain value. The |GainAdjustment| is simply
  // an interface that this object will use to apply the target gain values in |BehaviorGain|.
  // If no parameter is provided for |BehaviorGain|, a default behavior will be used.
  //
  // |gain_adjustment| must be non-null.
  AudioAdmin(StreamVolumeManager* volume_manager, PolicyActionReporter* policy_action_reporter,
             ActivityDispatcher* activity_dispatcher,
             ActiveStreamCountReporter* active_stream_count_reporter,
             async_dispatcher_t* fidl_dispatcher,
             BehaviorGain behavior_gain = kDefaultGainBehavior);

  // Sets the interaction behavior between |active| and |affected| usages.
  void SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                      fuchsia::media::Behavior behavior);

  // Clears all configured behaviors.
  void ResetInteractions() {
    std::lock_guard<fit::thread_checker> lock(fidl_thread_checker_);
    active_rules_.ResetInteractions();
  }

  // Clears all configured behaviors and then applies the rules in the provided AudioPolicy.
  void SetInteractionsFromAudioPolicy(AudioPolicy policy);

  // Interfaces used by AudioCoreImpl for active-stream accounting
  void UpdateRendererState(RenderUsage usage, bool active, fuchsia::media::AudioRenderer* renderer);
  void UpdateCapturerState(CaptureUsage usage, bool active,
                           fuchsia::media::AudioCapturer* capturer);

  bool IsActive(RenderUsage usage);
  bool IsActive(CaptureUsage usage);

 protected:
  using RendererPolicies = std::array<fuchsia::media::Behavior, fuchsia::media::RENDER_USAGE_COUNT>;
  using CapturerPolicies =
      std::array<fuchsia::media::Behavior, fuchsia::media::CAPTURE_USAGE_COUNT>;

  std::unordered_set<fuchsia::media::AudioRenderer*>* active_streams_playback() {
    return active_streams_playback_;
  }
  std::unordered_set<fuchsia::media::AudioCapturer*>* active_streams_capture() {
    return active_streams_capture_;
  }

  // Used to ensure we are on the thread where we constructed the class (should be the FIDL thread).
  async_dispatcher_t* fidl_dispatcher() { return fidl_dispatcher_; }

  // For static thread annotation to work properly, subclasses must directly access this member,
  // so it must be protected (despite guidance to make class member variables private).
  fit::thread_checker fidl_thread_checker_;

 private:
  friend class Reporter;

  const BehaviorGain behavior_gain_;
  StreamVolumeManager& stream_volume_manager_ FXL_GUARDED_BY(fidl_thread_checker_);
  PolicyActionReporter& policy_action_reporter_ FXL_GUARDED_BY(fidl_thread_checker_);
  ActivityDispatcher& activity_dispatcher_ FXL_GUARDED_BY(fidl_thread_checker_);
  ActiveStreamCountReporter* active_stream_count_reporter_ FXL_GUARDED_BY(fidl_thread_checker_);

  async_dispatcher_t* fidl_dispatcher_;

  void UpdatePolicy();
  void UpdateRenderActivity();
  void UpdateCaptureActivity();
  void UpdateActiveStreamCount(StreamUsage stream_usage)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(fidl_thread_checker_);

  // Helpers to make the control of streams cleaner.
  void SetUsageNone(fuchsia::media::AudioRenderUsage usage);
  void SetUsageNone(fuchsia::media::AudioCaptureUsage usage);

  void SetUsageMute(fuchsia::media::AudioRenderUsage usage);
  void SetUsageMute(fuchsia::media::AudioCaptureUsage usage);

  void SetUsageDuck(fuchsia::media::AudioRenderUsage usage);
  void SetUsageDuck(fuchsia::media::AudioCaptureUsage usage);

  void ApplyNewPolicies(const RendererPolicies& new_renderer_policies,
                        const CapturerPolicies& new_capturer_policies);

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
      active_streams_playback_[kStreamRenderUsageCount] FXL_GUARDED_BY(fidl_thread_checker_);
  std::unordered_set<fuchsia::media::AudioCapturer*>
      active_streams_capture_[kStreamCaptureUsageCount] FXL_GUARDED_BY(fidl_thread_checker_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_AUDIO_ADMIN_H_
