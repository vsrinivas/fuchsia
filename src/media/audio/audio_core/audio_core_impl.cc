// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_core_impl.h"

#include <lib/async/cpp/task.h>

#include "src/media/audio/audio_core/audio_capturer_impl.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {
// All audio renderer buffers will need to fit within this VMAR. We want to choose a size here large
// enough that will accomodate all the mappings required by all clients while also being small
// enough to avoid unnecessary page table fragmentation.
constexpr size_t kAudioRendererVmarSize = 16ull * 1024 * 1024 * 1024;
constexpr zx_vm_option_t kAudioRendererVmarFlags =
    ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_1GB;

}  // namespace

AudioCoreImpl::AudioCoreImpl(ThreadingModel* threading_model,
                             std::unique_ptr<sys::ComponentContext> component_context,
                             CommandLineOptions options)
    : threading_model_(*threading_model),
      device_settings_persistence_(threading_model),
      device_manager_(threading_model, &route_graph_, &device_settings_persistence_),
      volume_manager_(threading_model->FidlDomain().dispatcher()),
      audio_admin_(this, threading_model->FidlDomain().dispatcher(), &usage_reporter_),
      route_graph_(ProcessConfig::instance().routing_config()),
      component_context_(std::move(component_context)),
      vmar_manager_(
          fzl::VmarManager::Create(kAudioRendererVmarSize, nullptr, kAudioRendererVmarFlags)) {
  FX_DCHECK(vmar_manager_ != nullptr) << "Failed to allocate VMAR";

  // The main async_t here is responsible for receiving audio payloads sent by applications, so it
  // has real time requirements just like mixing threads. Ideally, this task would not run on the
  // same thread that processes *all* non-mix audio service jobs (even non-realtime ones), but that
  // will take more significant restructuring, when we can deal with realtime requirements in place.
  AcquireAudioCoreImplProfile(component_context_.get(), [](zx::profile profile) {
    FX_DCHECK(profile);
    if (profile) {
      zx_status_t status = zx::thread::self()->set_profile(profile, 0);
      FX_DCHECK(status == ZX_OK);
    }
  });

  device_manager_.EnableDeviceSettings(options.enable_device_settings_writeback);
  zx_status_t res = device_manager_.Init();
  FX_DCHECK(res == ZX_OK);

  route_graph_.SetThrottleOutput(threading_model,
                                 ThrottleOutput::Create(threading_model, &device_manager_));

  // Set up our audio policy.
  LoadDefaults();

  PublishServices();
}

AudioCoreImpl::~AudioCoreImpl() { Shutdown(); }

void AudioCoreImpl::PublishServices() {
  component_context_->outgoing()->AddPublicService<fuchsia::media::AudioCore>(
      [this](fidl::InterfaceRequest<fuchsia::media::AudioCore> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  component_context_->outgoing()->AddPublicService<fuchsia::media::AudioDeviceEnumerator>(
      [this](fidl::InterfaceRequest<fuchsia::media::AudioDeviceEnumerator> request) {
        device_manager_.AddDeviceEnumeratorClient(std::move(request));
      });

  component_context_->outgoing()->AddPublicService<fuchsia::media::UsageReporter>(
      usage_reporter_.GetHandler());
}

void AudioCoreImpl::Shutdown() {
  TRACE_DURATION("audio", "AudioCoreImpl::Shutdown");
  device_manager_.Shutdown();
}

void AudioCoreImpl::CreateAudioRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) {
  TRACE_DURATION("audio", "AudioCoreImpl::CreateAudioRenderer");
  AUD_VLOG(TRACE);

  route_graph_.AddRenderer(AudioRendererImpl::Create(std::move(audio_renderer_request), this));
}

void AudioCoreImpl::CreateAudioCapturer(
    bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request) {
  TRACE_DURATION("audio", "AudioCoreImpl::CreateAudioCapturer");
  AUD_VLOG(TRACE);
  if (loopback) {
    route_graph_.AddLoopbackCapturer(
        AudioCapturerImpl::Create(loopback, std::move(audio_capturer_request), this));
  } else {
    route_graph_.AddCapturer(
        AudioCapturerImpl::Create(loopback, std::move(audio_capturer_request), this));
  }
}

void AudioCoreImpl::SetRenderUsageGain(fuchsia::media::AudioRenderUsage render_usage,
                                       float gain_db) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetRenderUsageGain");
  AUD_VLOG(TRACE) << " (render_usage: " << static_cast<int>(render_usage) << ", " << gain_db
                  << " dB)";
  volume_manager_.SetUsageGain(UsageFrom(render_usage), gain_db);
}

void AudioCoreImpl::SetCaptureUsageGain(fuchsia::media::AudioCaptureUsage capture_usage,
                                        float gain_db) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetCaptureUsageGain");
  AUD_VLOG(TRACE) << " (capture_usage: " << static_cast<int>(capture_usage) << ", " << gain_db
                  << " dB)";
  volume_manager_.SetUsageGain(UsageFrom(capture_usage), gain_db);
}

void AudioCoreImpl::SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage render_usage,
                                                 float db_gain) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetRenderUsageGainAdjustment");
  volume_manager_.SetUsageGainAdjustment(UsageFrom(render_usage), db_gain);
}

void AudioCoreImpl::SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage capture_usage,
                                                  float db_gain) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetCaptureUsageGainAdjustment");
  volume_manager_.SetUsageGainAdjustment(UsageFrom(capture_usage), db_gain);
}

void AudioCoreImpl::EnableDeviceSettings(bool enabled) {
  TRACE_DURATION("audio", "AudioCoreImpl::EnableDeviceSettings");
  AUD_VLOG(TRACE) << " (enabled: " << enabled << ")";
  device_manager().EnableDeviceSettings(enabled);
}

void AudioCoreImpl::BindUsageVolumeControl(
    fuchsia::media::Usage usage,
    fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> volume_control) {
  TRACE_DURATION("audio", "AudioCoreImpl::BindUsageVolumeControl");
  volume_manager_.BindUsageVolumeClient(std::move(usage), std::move(volume_control));
}

void AudioCoreImpl::SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                                   fuchsia::media::Behavior behavior) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetInteraction");
  audio_admin_.SetInteraction(std::move(active), std::move(affected), behavior);
}

void AudioCoreImpl::LoadDefaults() {
  TRACE_DURATION("audio", "AudioCoreImpl::LoadDefaults");
  auto policy = PolicyLoader::LoadDefaultPolicy();
  FX_CHECK(policy);
  audio_admin_.SetInteractionsFromAudioPolicy(std::move(*policy));
}

void AudioCoreImpl::ResetInteractions() {
  TRACE_DURATION("audio", "AudioCoreImpl::ResetInteractions");
  audio_admin_.ResetInteractions();
}

}  // namespace media::audio
