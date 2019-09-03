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

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/fwd_decls.h"
#include "src/media/audio/audio_core/policy_loader.h"

namespace media {
namespace audio {

class UsageGainAdjustment;

class AudioAdmin {
 public:
  struct BehaviorGain {
    float none_gain_db;
    float duck_gain_db;
    float mute_gain_db;
  };

  // Constructs an |AudioAdmin| from a |BehaviorGain| and |GainAdjustment|.
  //
  // The |BehaviorGain| provides the target gain_db values to use when triggering behaviors between
  // usages, simply mapping each behavior to a relative gain value. The |GainAdjustment| is simply
  // an interface that this object will use to apply the target gain values in |BehaviorGain|.
  //
  // |gain_adjustment| must be non-null.
  AudioAdmin(BehaviorGain behavior_gain, UsageGainAdjustment* gain_adjustment);

  // Constructs an |AudioAdmin| using some default |BehaviorGain| values.
  explicit AudioAdmin(UsageGainAdjustment* gain_adjustment)
      : AudioAdmin(
            BehaviorGain{
                .none_gain_db = 0.0f,
                .duck_gain_db = -14.0f,
                .mute_gain_db = fuchsia::media::audio::MUTED_GAIN_DB,
            },
            gain_adjustment) {}

  // Sets the interaction behavior between |active| and |affected| usages.
  void SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                      fuchsia::media::Behavior behavior);

  // Clears all configured behaviors.
  void ResetInteractions() { active_rules_.ResetInteractions(); };

  // Clears all configured behaviors and then applies the rules in the provided AudioPolicy.
  void SetInteractionsFromAudioPolicy(AudioPolicy policy);

  // Interface used by AudiolCoreImpl for accounting
  void UpdateRendererState(fuchsia::media::AudioRenderUsage usage, bool active,
                           fuchsia::media::AudioRenderer* renderer);
  void UpdateCapturerState(fuchsia::media::AudioCaptureUsage usage, bool active,
                           fuchsia::media::AudioCapturer* capturer);

 private:
  BehaviorGain behavior_gain_;
  UsageGainAdjustment& gain_adjustment_;

  void UpdatePolicy();

  // Helpers to make the control of streams cleaner.
  void SetUsageNone(fuchsia::media::AudioRenderUsage usage);
  void SetUsageNone(fuchsia::media::AudioCaptureUsage usage);

  void SetUsageMute(fuchsia::media::AudioRenderUsage usage);
  void SetUsageMute(fuchsia::media::AudioCaptureUsage usage);

  void SetUsageDuck(fuchsia::media::AudioRenderUsage usage);
  void SetUsageDuck(fuchsia::media::AudioCaptureUsage usage);

  // Helpers to make tracking accounting cleaner.
  bool IsActive(fuchsia::media::AudioRenderUsage usage);
  bool IsActive(fuchsia::media::AudioCaptureUsage usage);

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
  PolicyRules active_rules_;

  std::unordered_set<fuchsia::media::AudioRenderer*>
      active_streams_playback_[fuchsia::media::RENDER_USAGE_COUNT];
  std::unordered_set<fuchsia::media::AudioCapturer*>
      active_streams_capture_[fuchsia::media::CAPTURE_USAGE_COUNT];
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_ADMIN_H_
