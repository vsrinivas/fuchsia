// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_ADMIN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_ADMIN_H_

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

class AudioAdmin {
 public:
  AudioAdmin(AudioCoreImpl* service);
  ~AudioAdmin();
  zx_status_t Init();
  void Shutdown();

  // AudioPolicy interface
  void SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                      fuchsia::media::Behavior behavior);

  void ResetInteractions() { active_rules_.ResetInteractions(); };
  void LoadDefaults() { ::media::audio::PolicyLoader::LoadDefaults(this); };

  // Interface used by AudiolCoreImpl for accounting
  void UpdateRendererState(fuchsia::media::AudioRenderUsage usage, bool active,
                           fuchsia::media::AudioRenderer* renderer);
  void UpdateCapturerState(fuchsia::media::AudioCaptureUsage usage, bool active,
                           fuchsia::media::AudioCapturer* capturer);

 private:
  AudioCoreImpl* service_;

  void UpdatePolicy();

  float mute_gain_db_ = -160.0f;
  float duck_gain_db_ = -14.0f;
  float none_gain_db_ = 0.0f;

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
