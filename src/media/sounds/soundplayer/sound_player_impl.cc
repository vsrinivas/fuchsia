// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/sound_player_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/unique_fd.h>

#include "src/media/sounds/soundplayer/ogg_demux.h"
#include "src/media/sounds/soundplayer/wav_reader.h"

namespace soundplayer {

SoundPlayerImpl::SoundPlayerImpl(fuchsia::media::AudioPtr audio_service,
                                 fidl::InterfaceRequest<fuchsia::media::sounds::Player> request)
    : binding_(this), audio_service_(std::move(audio_service)) {
  FX_CHECK(request);
  FX_CHECK(audio_service_);

  audio_service_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(WARNING) << "SoundPlayerImpl: fuchsia.media.Audio connection closed, status " << status;
    DeleteThis();
  });

  WhenAudioServiceIsWarm([this, request = std::move(request)]() mutable {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(WARNING) << "SoundPlayerImpl: client connection closed, status " << status;
      DeleteThis();
    });
  });
}

SoundPlayerImpl::~SoundPlayerImpl() {}

void SoundPlayerImpl::AddSoundFromFile(uint32_t id,
                                       fidl::InterfaceHandle<class fuchsia::io::File> file,
                                       AddSoundFromFileCallback callback) {
  if (sounds_by_id_.find(id) != sounds_by_id_.end()) {
    FX_LOGS(WARNING) << "AddSoundFromFile called with id " << id
                     << " already in use, closing client connection";
    DeleteThis();
    return;
  }

  auto result = SoundFromFile(std::move(file));
  if (result.is_error()) {
    callback(fuchsia::media::sounds::Player_AddSoundFromFile_Result::WithErr(
        static_cast<int32_t>(result.error())));
    return;
  }

  callback(fuchsia::media::sounds::Player_AddSoundFromFile_Result::WithResponse(
      fuchsia::media::sounds::Player_AddSoundFromFile_Response(
          std::make_tuple(result.value()->duration().get()))));

  sounds_by_id_.emplace(id, result.take_value());
}

void SoundPlayerImpl::AddSoundBuffer(uint32_t id, fuchsia::mem::Buffer buffer,
                                     fuchsia::media::AudioStreamType stream_type) {
  if (sounds_by_id_.find(id) != sounds_by_id_.end()) {
    FX_LOGS(WARNING) << "AddSoundBuffer called with id " << id
                     << " already in use, closing client connection";
    DeleteThis();
    return;
  }

  sounds_by_id_.emplace(id, std::make_shared<UndiscardableSound>(std::move(buffer.vmo), buffer.size,
                                                                 std::move(stream_type)));
}

void SoundPlayerImpl::RemoveSound(uint32_t id) { sounds_by_id_.erase(id); }

void SoundPlayerImpl::PlaySound(uint32_t id, fuchsia::media::AudioRenderUsage usage,
                                PlaySoundCallback callback) {
  auto iter = sounds_by_id_.find(id);
  if (iter == sounds_by_id_.end()) {
    callback(fuchsia::media::sounds::Player_PlaySound_Result::WithErr(
        fuchsia::media::sounds::PlaySoundError::NO_SUCH_SOUND));
    return;
  }

  fuchsia::media::AudioRendererPtr audio_renderer;
  audio_service_->CreateAudioRenderer(audio_renderer.NewRequest());
  auto renderer = std::make_unique<Renderer>(std::move(audio_renderer), usage);
  auto renderer_raw_ptr = renderer.get();
  if (renderer->PlaySound(
          id, iter->second,
          [this, id, renderer_raw_ptr,
           callback = std::move(callback)](fuchsia::media::sounds::Player_PlaySound_Result result) {
            auto iter = renderers_by_sound_id_.find(id);
            if (iter != renderers_by_sound_id_.end() && iter->second == renderer_raw_ptr) {
              renderers_by_sound_id_.erase(iter);
            }

            renderers_.erase(renderer_raw_ptr);

            callback(std::move(result));
          }) == ZX_OK) {
    renderers_by_sound_id_.insert_or_assign(id, renderer_raw_ptr);
    renderers_.emplace(renderer_raw_ptr, std::move(renderer));
  } else {
    callback(fuchsia::media::sounds::Player_PlaySound_Result::WithErr(
        fuchsia::media::sounds::PlaySoundError::RENDERER_FAILED));
  }
}

void SoundPlayerImpl::StopPlayingSound(uint32_t id) {
  auto iter = renderers_by_sound_id_.find(id);
  if (iter == renderers_by_sound_id_.end()) {
    // The specified sound isn't playing.
    return;
  }

  iter->second->StopPlayingSound();
}

void SoundPlayerImpl::DeleteThis() {
  audio_service_.set_error_handler(nullptr);
  audio_service_ = nullptr;

  binding_.set_error_handler(nullptr);
  binding_.Unbind();

  delete this;
}

void SoundPlayerImpl::WhenAudioServiceIsWarm(fit::closure callback) {
  audio_service_->CreateAudioRenderer(audio_renderer_.NewRequest());

  audio_renderer_->SetPcmStreamType(
      fuchsia::media::AudioStreamType{.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                                      .channels = 1,
                                      .frames_per_second = 48000});

  audio_renderer_->EnableMinLeadTimeEvents(true);
  audio_renderer_.events().OnMinLeadTimeChanged =
      [this, callback = std::move(callback)](int64_t min_lead_time_nsec) {
        if (min_lead_time_nsec > 0) {
          callback();
          audio_renderer_ = nullptr;
        }
      };

  audio_renderer_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(WARNING) << "fuchsia.media.AudioRenderer connection closed waiting for warm-up, "
                        "closing client connection.";
    audio_renderer_.set_error_handler(nullptr);
    audio_renderer_ = nullptr;
    DeleteThis();
  });
}

fpromise::result<std::shared_ptr<Sound>, zx_status_t> SoundPlayerImpl::SoundFromFile(
    fidl::InterfaceHandle<fuchsia::io::File> file) {
  FX_DCHECK(file);

  fbl::unique_fd fd;
  zx_status_t status = fdio_fd_create(file.TakeChannel().release(), fd.reset_and_get_address());
  if (status != ZX_OK) {
    return fpromise::error(status);
  }

  auto sound = std::make_shared<DiscardableSound>(std::move(fd));

  {
    OggDemux demux;
    if (demux.Process(*sound) == ZX_OK) {
      // The raw pointer to |sound| is safe here, because the sound owns the callback.
      sound->SetRestoreCallback([sound = sound.get()]() {
        OggDemux demux;
        zx_status_t status = demux.Process(*sound);
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Failed to restore discarded ogg/opus sound";
        }
      });

      return fpromise::ok(std::move(sound));
    }
  }

  WavReader wav_reader;
  status = wav_reader.Process(*sound);
  if (status == ZX_OK) {
    // The raw pointer to |sound| is safe here, because the sound owns the callback.
    sound->SetRestoreCallback([sound = sound.get()]() {
      WavReader wav_reader;
      zx_status_t status = wav_reader.Process(*sound);
      if (status != ZX_OK) {
        FX_PLOGS(WARNING, status) << "Failed to restore discarded WAV sound";
      }
    });

    return fpromise::ok(std::move(sound));
  }

  return fpromise::error(status);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// SoundPlayerImpl::Renderer

SoundPlayerImpl::Renderer::Renderer(fuchsia::media::AudioRendererPtr audio_renderer,
                                    fuchsia::media::AudioRenderUsage usage)
    : audio_renderer_(std::move(audio_renderer)) {
  audio_renderer_->SetUsage(usage);
}

SoundPlayerImpl::Renderer::~Renderer() {}

zx_status_t SoundPlayerImpl::Renderer::PlaySound(uint32_t id, std::shared_ptr<Sound> sound,
                                                 PlaySoundCallback completion_callback) {
  audio_renderer_->SetPcmStreamType(fidl::Clone(sound->stream_type()));

  zx::vmo vmo;
  auto status = sound->LockForRead().duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to duplicate VMO handle";
    sound->Unlock();
    return status;
  }

  locked_sound_ = sound;

  play_sound_callback_ = std::move(completion_callback);

  audio_renderer_->AddPayloadBuffer(0, std::move(vmo));

  uint64_t frames_remaining = sound->frame_count();
  uint64_t offset = 0;

  while (frames_remaining != 0) {
    uint64_t frames_to_send = std::min(
        frames_remaining, static_cast<uint64_t>(fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET));

    fuchsia::media::StreamPacket packet{
        .pts = fuchsia::media::NO_TIMESTAMP,
        .payload_buffer_id = 0,
        .payload_offset = offset,
        .payload_size = frames_to_send * sound->frame_size(),
        .flags = 0,
        .buffer_config = 0,
        .stream_segment_id = 0,
    };

    if (frames_to_send == frames_remaining) {
      audio_renderer_->SendPacket(std::move(packet), [this]() {
        locked_sound_->Unlock();
        locked_sound_ = nullptr;
        // This renderer may be deleted during the callback, so we move the callback to prevent
        // it from being deleted out from under us.
        auto callback = std::move(play_sound_callback_);
        callback(fuchsia::media::sounds::Player_PlaySound_Result::WithResponse(
            fuchsia::media::sounds::Player_PlaySound_Response()));
      });
    } else {
      audio_renderer_->SendPacketNoReply(std::move(packet));
    }

    frames_remaining -= frames_to_send;
    offset += frames_to_send * sound->frame_size();
  }

  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, 0);

  // No cleanup here. This renderer is deleted when the sound finishes playing or when playback
  // is stopped by |StopPlayingSound|. When the renderer is deleted, the FIDL AudioRenderer is
  // destroyed, so no cleanup of the AudioRenderer is required.

  return ZX_OK;
}

void SoundPlayerImpl::Renderer::StopPlayingSound() {
  audio_renderer_ = nullptr;
  if (play_sound_callback_) {
    locked_sound_->Unlock();
    locked_sound_ = nullptr;

    // This renderer may be deleted during the callback, so we move the callback to prevent
    // it from being deleted out from under us.
    auto callback = std::move(play_sound_callback_);
    callback(fuchsia::media::sounds::Player_PlaySound_Result::WithErr(
        fuchsia::media::sounds::PlaySoundError::STOPPED));
  }
}

}  // namespace soundplayer
