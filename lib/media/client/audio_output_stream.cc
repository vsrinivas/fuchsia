// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/media/client/audio_output_stream.h"

#include <fbl/algorithm.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <cmath>

#include "garnet/lib/media/client/audio_output_device.h"
#include "garnet/lib/media/client/audio_output_manager.h"

#include <fuchsia/media/cpp/fidl.h>
#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/media/timeline/timeline.h"

namespace media_client {

namespace {
constexpr size_t kBufferId = 0;
}  // namespace

AudioOutputStream::AudioOutputStream() {}

AudioOutputStream::~AudioOutputStream() { Stop(); }

bool AudioOutputStream::Initialize(fuchsia_audio_parameters* params,
                                   AudioOutputDevice* device) {
  num_channels_ = params->num_channels;
  sample_rate_ = params->sample_rate;
  device_ = device;
  total_mapping_samples_ = sample_rate_ * num_channels_;

  if (!AcquireRenderer()) {
    FXL_LOG(ERROR) << "AcquireRenderer failed";
    return false;
  }
  if (!SetMediaType(num_channels_, sample_rate_)) {
    FXL_LOG(ERROR) << "SetMediaType failed";
    return false;
  }
  if (!CreateMemoryMapping()) {
    FXL_LOG(ERROR) << "CreateMemoryMapping failed";
    return false;
  }
  if (!GetDelays()) {
    FXL_LOG(ERROR) << "GetDelays failed";
    return false;
  }

  active_ = true;
  return true;
};

bool AudioOutputStream::AcquireRenderer() {
  fuchsia::media::AudioSync2Ptr audio;
  fuchsia::sys::ConnectToEnvironmentService(audio.NewRequest());

  if (audio
          ->CreateRenderer(audio_renderer_.NewRequest(),
                           media_renderer_.NewRequest())
          .statvs != ZX_OK) {
    return false;
  }

  return media_renderer_
             ->GetTimelineControlPoint(timeline_control_point_.NewRequest())
             .statvs == ZX_OK;
}

bool AudioOutputStream::SetMediaType(int num_channels, int sample_rate) {
  if ((num_channels < static_cast<int>(fuchsia::media::kMinLpcmChannelCount)) ||
      (num_channels > static_cast<int>(fuchsia::media::kMaxLpcmChannelCount)) ||
      (sample_rate <
       static_cast<int>(fuchsia::media::kMinLpcmFramesPerSecond)) ||
      (sample_rate >
       static_cast<int>(fuchsia::media::kMaxLpcmFramesPerSecond))) {
    FXL_LOG(ERROR) << "Media type (" << num_channels << "-chan, " << sample_rate
                   << " Hz) out of range";
    return false;
  }

  FXL_DCHECK(media_renderer_);

  fuchsia::media::AudioMediaTypeDetails details;
  details.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  details.channels = num_channels;
  details.frames_per_second = sample_rate;

  fuchsia::media::MediaType media_type;
  media_type.medium = fuchsia::media::MediaTypeMedium::AUDIO;
  media_type.encoding = fuchsia::media::kAudioEncodingLpcm;
  media_type.details.set_audio(std::move(details));

  if (media_renderer_->SetMediaType(std::move(media_type)).statvs == ZX_OK) {
    FXL_LOG(ERROR) << "Could not set media type";
    return false;
  }
  return true;
}

bool AudioOutputStream::CreateMemoryMapping() {
  zx_status_t status =
      zx::vmo::create(total_mapping_samples_ * sizeof(float), 0, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmo::create failed - " << status;
    return false;
  }

  uintptr_t mapped_address;
  status = zx::vmar::root_self().map(
      0, vmo_, 0, total_mapping_samples_ * sizeof(float),
      ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, &mapped_address);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_vmar_map failed - " << status;
    return false;
  }
  buffer_ = reinterpret_cast<float*>(mapped_address);

  zx::vmo duplicate_vmo;
  status = vmo_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                          &duplicate_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::handle::duplicate failed - " << status;
    return false;
  }

  if (media_renderer_->GetPacketConsumer(packet_consumer_.NewRequest())
          .statvs != ZX_OK) {
    FXL_LOG(ERROR) << "PacketConsumer connection lost. Quitting.";
    return false;
  }

  FXL_DCHECK(packet_consumer_);
  if (packet_consumer_->AddPayloadBuffer(kBufferId, std::move(duplicate_vmo))
          .statvs != ZX_OK) {
    FXL_LOG(ERROR) << "Could not add payload buffer";
    return false;
  }

  return true;
}

bool AudioOutputStream::GetDelays() {
  if (audio_renderer_->GetMinDelay(&delay_nsec_).statvs != ZX_OK) {
    Stop();
    FXL_LOG(ERROR) << "GetMinDelay failed";
    return false;
  }

  return true;
}

void AudioOutputStream::PullFromClientBuffer(float* client_buffer,
                                             int num_samples) {
  FXL_DCHECK(current_sample_offset_ + num_samples <= total_mapping_samples_);

  for (int idx = 0; idx < num_samples; ++idx) {
    buffer_[idx + current_sample_offset_] = client_buffer[idx];
  }
  current_sample_offset_ =
      (current_sample_offset_ + num_samples) % total_mapping_samples_;
}

fuchsia::media::MediaPacket AudioOutputStream::CreateMediaPacket(
    zx_time_t pts, size_t payload_offset, size_t payload_size) {
  fuchsia::media::MediaPacket packet;

  packet.pts_rate_ticks = sample_rate_;
  packet.pts_rate_seconds = 1;
  packet.flags = 0u;
  packet.payload_buffer_id = kBufferId;
  packet.payload_size = payload_size;
  packet.payload_offset = payload_offset;
  packet.pts = pts;

  return packet;
}

bool AudioOutputStream::SendMediaPacket(fuchsia::media::MediaPacket packet) {
  FXL_DCHECK(packet_consumer_);

  return packet_consumer_->SupplyPacketNoReply(std::move(packet)).statvs ==
         ZX_OK;
}

int AudioOutputStream::GetMinDelay(zx_duration_t* delay_nsec_out) {
  FXL_DCHECK(delay_nsec_out);

  if (!active_) {
    return ZX_ERR_CONNECTION_ABORTED;
  }

  *delay_nsec_out = delay_nsec_;
  return ZX_OK;
}

int AudioOutputStream::SetGain(float db_gain) {
  if (!active_) {
    return ZX_ERR_CONNECTION_ABORTED;
  }

  if (db_gain > fuchsia::media::kMaxGain) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (db_gain != renderer_db_gain_) {
    if (audio_renderer_->SetGain(db_gain).statvs != ZX_OK) {
      Stop();
      FXL_LOG(ERROR) << "SetGain failed";
      return ZX_ERR_CONNECTION_ABORTED;
    }
    renderer_db_gain_ = db_gain;
  }
  return ZX_OK;
}

int AudioOutputStream::Write(float* client_buffer, int num_samples,
                             zx_time_t pres_time) {
  FXL_DCHECK(client_buffer);
  FXL_DCHECK(pres_time <= FUCHSIA_AUDIO_NO_TIMESTAMP);
  FXL_DCHECK(num_samples > 0);
  FXL_DCHECK(num_samples % num_channels_ == 0);

  if (!active_)
    return ZX_ERR_CONNECTION_ABORTED;

  if (num_samples > total_mapping_samples_)
    return ZX_ERR_OUT_OF_RANGE;

  if (pres_time == FUCHSIA_AUDIO_NO_TIMESTAMP && !received_first_frame_)
    return ZX_ERR_BAD_STATE;

  // Don't copy beyond our internal buffer. Track the excess; handle it later.
  size_t overflow_samples = 0;
  if (current_sample_offset_ + num_samples > total_mapping_samples_) {
    overflow_samples =
        current_sample_offset_ + num_samples - total_mapping_samples_;
    num_samples -= overflow_samples;
  }

  // PullFromClientBuffer updates current_sample_offset_, so capture it here.
  size_t current_byte_offset = current_sample_offset_ * sizeof(float);
  PullFromClientBuffer(client_buffer, num_samples);

  // On first packet, establish a timeline starting at given presentation time.
  // Others get kNoTimestamp, indicating 'play without gap after the previous'.
  zx_time_t subject_time = fuchsia::media::kNoTimestamp;
  if (!received_first_frame_) {
    subject_time = 0;
    start_time_ = pres_time;
  }

  if (!SendMediaPacket(CreateMediaPacket(subject_time, current_byte_offset,
                                         num_samples * sizeof(float)))) {
    Stop();
    FXL_LOG(ERROR) << "SendMediaPacket failed";
    return ZX_ERR_CONNECTION_ABORTED;
  }
  if (!received_first_frame_) {
    if (!Start()) {
      Stop();
      FXL_LOG(ERROR) << "Start (SetTimelineTransform) failed";
      return ZX_ERR_CONNECTION_ABORTED;
    }
    received_first_frame_ = true;
  }

  // TODO(mpuryear): don't recurse; refactor to a helper func & call it twice.
  // For samples we couldn't send earlier, send them now (since we've wrapped)
  if (overflow_samples > 0) {
    return Write(client_buffer + num_samples, overflow_samples,
                 FUCHSIA_AUDIO_NO_TIMESTAMP);
  }

  return ZX_OK;
}

bool AudioOutputStream::Start() {
  fuchsia::media::TimelineConsumerSync2Ptr timeline_consumer;
  timeline_control_point_->GetTimelineConsumer(timeline_consumer.NewRequest());

  fuchsia::media::TimelineTransform transform;
  transform.reference_time = start_time_;
  transform.subject_time = 0;
  transform.reference_delta = 1;
  transform.subject_delta = 1;

  return timeline_consumer->SetTimelineTransformNoReply(std::move(transform))
             .statvs == ZX_OK;
}

void AudioOutputStream::Stop() {
  received_first_frame_ = false;
  active_ = false;

  fuchsia::media::TimelineConsumerSync2Ptr timeline_consumer;
  timeline_control_point_->GetTimelineConsumer(timeline_consumer.NewRequest());

  fuchsia::media::TimelineTransform transform;
  transform.reference_time = fuchsia::media::kUnspecifiedTime;
  transform.subject_time = fuchsia::media::kUnspecifiedTime;
  transform.reference_delta = 1;
  transform.subject_delta = 0;

  timeline_consumer->SetTimelineTransformNoReply(std::move(transform));
}

}  // namespace media_client
