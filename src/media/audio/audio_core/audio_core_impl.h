// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CORE_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CORE_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/sys/cpp/component_context.h>

#include <trace/event.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/command_line_options.h"
#include "src/media/audio/audio_core/link_matrix.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/usage_gain_adjustment.h"
#include "src/media/audio/audio_core/usage_reporter_impl.h"

namespace media::audio {

class AudioCoreImpl : public fuchsia::media::AudioCore, UsageGainAdjustment {
 public:
  AudioCoreImpl(ThreadingModel* threading_model,
                std::unique_ptr<sys::ComponentContext> component_context,
                CommandLineOptions options);

  // Disallow copy & move.
  AudioCoreImpl(AudioCoreImpl&& o) = delete;
  AudioCoreImpl& operator=(AudioCoreImpl&& o) = delete;
  AudioCoreImpl(const AudioCoreImpl&) = delete;
  AudioCoreImpl& operator=(const AudioCoreImpl&) = delete;

  ~AudioCoreImpl() override;

  ThreadingModel& threading_model() { return threading_model_; }
  AudioDeviceManager& device_manager() { return device_manager_; }
  AudioAdmin& audio_admin() { return audio_admin_; }
  fbl::RefPtr<fzl::VmarManager> vmar() const { return vmar_manager_; }
  StreamVolumeManager& volume_manager() { return volume_manager_; }
  RouteGraph& route_graph() { return route_graph_; }
  LinkMatrix& link_matrix() { return link_matrix_; }

 private:
  // |fuchsia::media::AudioCore|
  void CreateAudioRenderer(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) final;
  void CreateAudioCapturer(
      bool loopback,
      fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request) final;
  void EnableDeviceSettings(bool enabled) final;
  void SetRenderUsageGain(fuchsia::media::AudioRenderUsage usage, float gain_db) final;
  void SetCaptureUsageGain(fuchsia::media::AudioCaptureUsage usage, float gain_db) final;
  void BindUsageVolumeControl(
      fuchsia::media::Usage usage,
      fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> volume_control) final;
  void SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                      fuchsia::media::Behavior behavior) final;
  void ResetInteractions() final;
  void LoadDefaults() final;

 private:
  // |UsageGainAdjustment|
  void SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage, float gain_db) override;
  void SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage usage,
                                     float gain_db) override;

  void PublishServices();
  void Shutdown();

  fidl::BindingSet<fuchsia::media::AudioCore> bindings_;

  ThreadingModel& threading_model_;

  AudioDeviceSettingsPersistence device_settings_persistence_;
  // State for dealing with devices.
  AudioDeviceManager device_manager_;

  // Router for volume changes.
  StreamVolumeManager volume_manager_;

  UsageReporterImpl usage_reporter_;

  // Audio usage manager
  AudioAdmin audio_admin_;

  LinkMatrix link_matrix_;
  RouteGraph route_graph_;

  std::unique_ptr<sys::ComponentContext> component_context_;

  // We allocate a sub-vmar to hold the audio renderer buffers. Keeping these in a sub-vmar allows
  // us to take advantage of ASLR while minimizing page table fragmentation.
  fbl::RefPtr<fzl::VmarManager> vmar_manager_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CORE_IMPL_H_
