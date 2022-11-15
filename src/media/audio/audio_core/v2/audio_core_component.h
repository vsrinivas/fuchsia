// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_AUDIO_CORE_COMPONENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_AUDIO_CORE_COMPONENT_H_

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace-provider/provider.h>

#include "src/media/audio/audio_core/shared/activity_dispatcher.h"
#include "src/media/audio/audio_core/shared/audio_policy.h"
#include "src/media/audio/audio_core/shared/device_lister.h"
#include "src/media/audio/audio_core/shared/process_config.h"
#include "src/media/audio/audio_core/shared/profile_provider.h"
#include "src/media/audio/audio_core/shared/stream_volume_manager.h"
#include "src/media/audio/audio_core/shared/usage_gain_reporter_impl.h"
#include "src/media/audio/audio_core/shared/usage_reporter_impl.h"

namespace media_audio {

// Everything needed to run the "audio_core" component.
class AudioCoreComponent {
 public:
  // Start running the service. Discoverable protocols are published to `outgoing` and served from
  // `fidl_dispatcher`. Background tasks run on `io_dispatcher`. Cobalt reporting is enabled iff
  // `enable_cobalt`.
  AudioCoreComponent(sys::ComponentContext& component_context, async_dispatcher_t* fidl_dispatcher,
                     async_dispatcher_t* io_dispatcher, bool enable_cobalt);

 private:
  // TODO(fxbug.dev/98652): delete when we have a real implementation
  class EmptyDeviceLister : public media::audio::DeviceLister {
    std::vector<fuchsia::media::AudioDeviceInfo> GetDeviceInfos() { return {}; }
  };

  // Configs.
  media::audio::ProcessConfig process_config_;
  media::audio::AudioPolicy policy_config_;

  // Objects that serve discoverable FIDL protocols.
  std::unique_ptr<media::audio::ActivityDispatcherImpl> activity_dispatcher_;
  std::unique_ptr<media::audio::ProfileProvider> profile_provider_;
  std::unique_ptr<media::audio::UsageGainReporterImpl> usage_gain_reporter_;
  std::unique_ptr<media::audio::UsageReporterImpl> usage_reporter_;

  // TODO(fxbug.dev/98652):
  // fuchsia.media.Audio
  // fuchsia.media.AudioCore
  // fuchsia.media.AudioDeviceEnumerator
  // fuchsia.media.tuning.AudioTuner

  // Misc objects.
  EmptyDeviceLister empty_device_lister_;

  std::unique_ptr<trace::TraceProviderWithFdio> trace_provider_;
  std::unique_ptr<media::audio::StreamVolumeManager> stream_volume_manager_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_AUDIO_CORE_COMPONENT_H_
