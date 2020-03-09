// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_core_impl.h"

#include <lib/async/cpp/task.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/base_capturer.h"
#include "src/media/audio/audio_core/base_renderer.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

AudioCoreImpl::AudioCoreImpl(Context* context) : context_(*context) {
  FX_DCHECK(context);
  // The main async_t here is responsible for receiving audio payloads sent by applications, so it
  // has real time requirements just like mixing threads. Ideally, this task would not run on the
  // same thread that processes *all* non-mix audio service jobs (even non-realtime ones), but that
  // will take more significant restructuring, when we can deal with realtime requirements in place.
  AcquireAudioCoreImplProfile(&context_.component_context(), [](zx::profile profile) {
    FX_DCHECK(profile);
    if (profile) {
      zx_status_t status = zx::thread::self()->set_profile(profile, 0);
      FX_DCHECK(status == ZX_OK);
    }
  });

  // Set up our audio policy.
  LoadDefaults();

  context_.component_context().outgoing()->AddPublicService(bindings_.GetHandler(this));
}

AudioCoreImpl::~AudioCoreImpl() { Shutdown(); }

void AudioCoreImpl::Shutdown() {
  TRACE_DURATION("audio", "AudioCoreImpl::Shutdown");
  context_.device_manager().Shutdown();
}

void AudioCoreImpl::CreateAudioRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) {
  TRACE_DURATION("audio", "AudioCoreImpl::CreateAudioRenderer");
  AUD_VLOG(TRACE);

  context_.route_graph().AddRenderer(
      BaseRenderer::Create(std::move(audio_renderer_request), &context_));
}

void AudioCoreImpl::CreateAudioCapturerWithConfiguration(
    fuchsia::media::AudioStreamType stream_type, fuchsia::media::AudioCaptureUsage usage,
    fuchsia::media::AudioCapturerConfiguration configuration,
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request) {
  auto format = Format::Create(stream_type);
  if (format.is_error()) {
    FX_LOGS(WARNING) << "Attempted to create AudioCapturer with invalid stream type";
    return;
  }

  context_.route_graph().AddLoopbackCapturer(
      BaseCapturer::Create(std::move(configuration), {format.take_value()}, {usage},
                           std::move(audio_capturer_request), &context_));
}

void AudioCoreImpl::CreateAudioCapturer(
    bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request) {
  TRACE_DURATION("audio", "AudioCoreImpl::CreateAudioCapturer");
  AUD_VLOG(TRACE);

  if (loopback) {
    context_.route_graph().AddLoopbackCapturer(BaseCapturer::Create(
        fuchsia::media::AudioCapturerConfiguration::WithLoopback(
            fuchsia::media::LoopbackAudioCapturerConfiguration()),
        /*format= */ std::nullopt,
        /*usage= */ std::nullopt, std::move(audio_capturer_request), &context_));
    return;
  }

  context_.route_graph().AddCapturer(
      BaseCapturer::Create(fuchsia::media::AudioCapturerConfiguration::WithInput(
                               fuchsia::media::InputAudioCapturerConfiguration()),
                           /*format= */ std::nullopt,
                           /*usage= */ std::nullopt, std::move(audio_capturer_request), &context_));
}

void AudioCoreImpl::SetRenderUsageGain(fuchsia::media::AudioRenderUsage render_usage,
                                       float gain_db) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetRenderUsageGain");
  AUD_VLOG(TRACE) << " (render_usage: " << static_cast<int>(render_usage) << ", " << gain_db
                  << " dB)";
  context_.volume_manager().SetUsageGain(
      fuchsia::media::Usage::WithRenderUsage(std::move(render_usage)), gain_db);
}

void AudioCoreImpl::SetCaptureUsageGain(fuchsia::media::AudioCaptureUsage capture_usage,
                                        float gain_db) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetCaptureUsageGain");
  AUD_VLOG(TRACE) << " (capture_usage: " << static_cast<int>(capture_usage) << ", " << gain_db
                  << " dB)";
  context_.volume_manager().SetUsageGain(
      fuchsia::media::Usage::WithCaptureUsage(std::move(capture_usage)), gain_db);
}

void AudioCoreImpl::BindUsageVolumeControl(
    fuchsia::media::Usage usage,
    fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> volume_control) {
  TRACE_DURATION("audio", "AudioCoreImpl::BindUsageVolumeControl");
  context_.volume_manager().BindUsageVolumeClient(std::move(usage), std::move(volume_control));
}

void AudioCoreImpl::SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                                   fuchsia::media::Behavior behavior) {
  TRACE_DURATION("audio", "AudioCoreImpl::SetInteraction");
  context_.audio_admin().SetInteraction(std::move(active), std::move(affected), behavior);
}

void AudioCoreImpl::LoadDefaults() {
  TRACE_DURATION("audio", "AudioCoreImpl::LoadDefaults");
  auto policy = PolicyLoader::LoadDefaultPolicy();
  FX_CHECK(policy);
  context_.audio_admin().SetInteractionsFromAudioPolicy(std::move(*policy));
}

void AudioCoreImpl::ResetInteractions() {
  TRACE_DURATION("audio", "AudioCoreImpl::ResetInteractions");
  context_.audio_admin().ResetInteractions();
}

}  // namespace media::audio
