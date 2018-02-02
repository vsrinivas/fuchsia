// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_renderer2_impl.h"

#include <fbl/auto_call.h>

#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

fbl::RefPtr<AudioRenderer2Impl> AudioRenderer2Impl::Create(
    f1dl::InterfaceRequest<AudioRenderer2> audio_renderer_request,
    AudioServerImpl* owner) {
  return fbl::AdoptRef(new AudioRenderer2Impl(std::move(audio_renderer_request),
                                              owner));
}

AudioRenderer2Impl::AudioRenderer2Impl(
    f1dl::InterfaceRequest<AudioRenderer2> audio_renderer_request,
    AudioServerImpl* owner)
  : owner_(owner),
    audio_renderer_binding_(this, std::move(audio_renderer_request)) {

  audio_renderer_binding_.set_error_handler([this]() {
    audio_renderer_binding_.Unbind();
    Shutdown();
  });
}

AudioRenderer2Impl::~AudioRenderer2Impl() {
  // assert that we have been cleanly shutdown already.
  FXL_DCHECK(is_shutdown_);
  FXL_DCHECK(!audio_renderer_binding_.is_bound());
  FXL_DCHECK(gain_control_bindings_.size() == 0);
}

void AudioRenderer2Impl::Shutdown() {
  // If we have already been shutdown, then we are just waiting for the service
  // to destroy us.  Run some FXL_DCHECK sanity checks and get out.
  if (is_shutdown_) {
    FXL_DCHECK(!audio_renderer_binding_.is_bound());
    return;
  }

  is_shutdown_ = true;

  PreventNewLinks();
  Unlink();

  if (audio_renderer_binding_.is_bound()) {
    audio_renderer_binding_.Unbind();
  }

  gain_control_bindings_.CloseAll();
  payload_buffer_.reset();
}

void AudioRenderer2Impl::SnapshotCurrentTimelineFunction(
    int64_t reference_time,
    TimelineFunction* out,
    uint32_t* generation) {
  *generation = 0;
}

// IsOperating is true any time we have any packets in flight.  Most
// configuration functions cannot be called any time we are operational.
bool AudioRenderer2Impl::IsOperating() {
  if (throttle_output_link_ && !throttle_output_link_->pending_queue_empty()) {
    return true;
  }

  fbl::AutoLock links_lock(&links_lock_);
  // Renderers should never be linked to sources.
  FXL_DCHECK(source_links_.empty());

  for (const auto& link : dest_links_) {
    FXL_DCHECK(link->source_type() == AudioLink::SourceType::Packet);
    auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
    if (!packet_link->pending_queue_empty()) {
      return true;
    }
  }

  return false;
}

bool AudioRenderer2Impl::ValidateConfig() {
  if (config_validated_) {
    return true;
  }

  if (!format_info_valid() || (payload_buffer_ == nullptr)) {
    return false;
  }

  // TODO(johngro): Precompute anything we need to precompute here.  For
  // example, computing the pts continuity threshold should happen here.  Adding
  // links to other output (and selecting resampling filters) might belong here
  // as well.

  config_validated_ = true;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
//
// AudioRenderer2 Interface
//
void AudioRenderer2Impl::SetPcmFormat(AudioPcmFormatPtr format) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  // We cannot change the format while we are currently operational
  if (IsOperating()) {
    FXL_LOG(ERROR) << "Attempted to set format while in the operational mode.";
    return;
  }

  // Sanity check the requested format
  switch (format->sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
    case AudioSampleFormat::SIGNED_16:
      break;

    // TODO(johngro): Add more sample formats (24 bit, float, etc..) as the
    // mixer core learns to handle them.
    default:
      FXL_LOG(ERROR) << "Unsupported sample format ("
                     << format->sample_format
                     << ") in AudioRenderer::SetPcmFormat.";
      return;
  }

  if ((format->channels < AudioPcmFormat::kMinChannelCount) ||
      (format->channels > AudioPcmFormat::kMaxChannelCount)) {
      FXL_LOG(ERROR)
        << "Invalid channel count (" << format->channels
        << ") in AudioRenderer::SetPcmFormat.  Must be on the range ["
        << AudioPcmFormat::kMinChannelCount << ", "
        << AudioPcmFormat::kMaxChannelCount << "]";
      return;
  }

  if ((format->frames_per_second < AudioPcmFormat::kMinFramesPerSecond) ||
      (format->frames_per_second > AudioPcmFormat::kMaxFramesPerSecond)) {
      FXL_LOG(ERROR)
        << "Invalid frame rate (" << format->frames_per_second
        << ") in AudioRenderer::SetPcmFormat.  Must be on the range ["
        << AudioPcmFormat::kMinFramesPerSecond << ", "
        << AudioPcmFormat::kMaxFramesPerSecond << "]";
      return;
  }

  // Everything checks out.  Discard any existing links we are holding
  // onto.  New links need to be created with our new format.
  Unlink();

  // Create a new format info object so we can create links to outputs.
  // TODO(johngro): Look into eliminating most of the format_info class when we
  // finish removing the old audio renererer interface.  At the very least,
  // switch to using the AudioPcmFormat struct instead of AudioMediaTypeDetails
  AudioMediaTypeDetailsPtr cfg = AudioMediaTypeDetails::New();
  cfg->sample_format = format->sample_format;
  cfg->channels = format->channels;
  cfg->frames_per_second = format->frames_per_second;
  format_info_ = AudioRendererFormatInfo::Create(std::move(cfg));

  // Have the audio output manager initialize our set of outputs.  Note; there
  // is currently no need for a lock here.  Methods called from our user-facing
  // interfaces are seriailzed by nature of the fidl framework, and none of the
  // output manager's threads should ever need to manipulate the set.  Cleanup
  // of outputs which have gone away is currently handled in a lazy fashion when
  // the renderer fails to promote its weak reference during an operation
  // involving its outputs.
  //
  // TODO(johngro): someday, we will need to deal with recalculating properties
  // which depend on a renderer's current set of outputs (for example, the
  // minimum latency).  This will probably be done using a dirty flag in the
  // renderer implementations, and scheduling a job to recalculate the
  // properties for the dirty renderers and notify the users as appropriate.

  // If we cannot promote our own weak pointer, something is seriously wrong.
  owner_->GetDeviceManager().SelectOutputsForRenderer(this);

  // Things went well, cancel the cleanup hook.  If our config had been
  // validated previously, it will have to be revalidated as we move into the
  // operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRenderer2Impl::SetPayloadBuffer(zx::vmo payload_buffer) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
      << "Attempted to set payload buffer while in the operational mode.";
    return;
  }

  // TODO(johngro) : MTWN-69
  // Map this into a sub-vmar instead of defaulting to the root
  // once teisenbe@ provides guidance on the best-practice for doing this.
  zx_status_t res;
  payload_buffer_ = fbl::AdoptRef(new fbl::RefCountedVmoMapper());
  res = payload_buffer_->Map(payload_buffer, 0, ZX_VM_FLAG_PERM_READ);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map payload buffer (res = " << res << ")";
    return;
  }

  // Things went well, cancel the cleanup hook.  If our config had been
  // validated previously, it will have to be revalidated as we move into the
  // operational phase of our life.
  config_validated_ = false;
  cleanup.cancel();
}

void AudioRenderer2Impl::SetPtsUnits(uint32_t tick_per_second_numerator,
                                     uint32_t tick_per_second_denominator) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
      << "Attempted to set PTS units while in the operational mode.";
    return;
  }

  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::SetPtsContinuityThreshold(float threshold_seconds) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
      << "Attempted to set PTS cont threshold while in the operational mode.";
    return;
  }

  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::SetReferenceClock(zx::handle ref_clock) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (IsOperating()) {
    FXL_LOG(ERROR)
      << "Attempted to set reference clock while in the operational mode.";
    return;
  }

  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::SendPacket(AudioPacketPtr packet,
                                    const SendPacketCallback& callback) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (!ValidateConfig()) {
    FXL_LOG(ERROR) << "Failed to validate configuration during SendPacket";
    return;
  }

  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::SendPacketNoReply(AudioPacketPtr packet) {
  SendPacket(std::move(packet), nullptr);
}

void AudioRenderer2Impl::Flush(const FlushCallback& callback) {
  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
  Shutdown();
}

void AudioRenderer2Impl::FlushNoReply() {
  Flush(nullptr);
}

void AudioRenderer2Impl::Play(int64_t reference_time,
                              int64_t media_time,
                              const PlayCallback& callback) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (!ValidateConfig()) {
    FXL_LOG(ERROR) << "Failed to validate configuration during Play";
    return;
  }

  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::PlayNoReply(int64_t reference_time,
                                     int64_t media_time) {
  Play(reference_time, media_time, nullptr);
}

void AudioRenderer2Impl::Pause(const PauseCallback& callback) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });

  if (!ValidateConfig()) {
    FXL_LOG(ERROR) << "Failed to validate configuration during Pause";
    return;
  }

  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::PauseNoReply() { Pause(nullptr); }

void AudioRenderer2Impl::SetGainMute(float gain,
                                     bool mute,
                                     uint32_t flags,
                                     const SetGainMuteCallback& callback) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });
  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::SetGainMuteNoReply(float gain,
                                            bool mute,
                                            uint32_t flags) {
  SetGainMute(gain, mute, flags, nullptr);
}

void AudioRenderer2Impl::DuplicateGainControlInterface(
    f1dl::InterfaceRequest<AudioRendererGainControl> request) {
  gain_control_bindings_.AddBinding(GainControlBinding::Create(this),
                                    std::move(request));
}

void AudioRenderer2Impl::EnableMinLeadTimeEvents(
    f1dl::InterfaceHandle<AudioRendererMinLeadTimeChangedEvent> evt) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });
  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

void AudioRenderer2Impl::GetMinLeadTime(
    const GetMinLeadTimeCallback& callback) {
  auto cleanup = fbl::MakeAutoCall([this]() { Shutdown(); });
  FXL_LOG(WARNING) << "Not Implemented : " << __PRETTY_FUNCTION__;
}

// Shorthand to save horizontal space for the thunks which follow.
void AudioRenderer2Impl::GainControlBinding::SetGainMute(
    float gain, bool mute, uint32_t flags,
    const SetGainMuteCallback& callback) {
  owner_->SetGainMute(gain, mute, flags, callback);
}

void AudioRenderer2Impl::GainControlBinding::SetGainMuteNoReply(
    float gain, bool mute, uint32_t flags) {
  owner_->SetGainMuteNoReply(gain, mute, flags);
}

}  // namespace audio
}  // namespace media
