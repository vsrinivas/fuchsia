// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/media/audio_output_stream.h"

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "garnet/lib/media/audio_output_device.h"
#include "garnet/lib/media/audio_output_manager.h"
#include "garnet/lib/media/limits.h"
#include "garnet/public/lib/app/cpp/environment_services.h"
#include "garnet/public/lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/audio_server.fidl.h"
#include "lib/media/timeline/timeline.h"

namespace media_client {

namespace {
constexpr size_t kBufferId = 0;
// If we're asked to synthesize a start time for the first frame, add this
// adjustment to the minimum delay time to reduce the chance that we
// accidentally clip the start of the first sample during mixing. If the caller
// specifies a start time for the first frame (which is the expected mode of
// operation) we will use that value without adjustement.
constexpr zx_duration_t kSyntheticStartTimeAdjustment = ZX_MSEC(5);
}  // namespace

AudioOutputStream::AudioOutputStream() {}

AudioOutputStream::~AudioOutputStream() {
  Stop();
}

bool AudioOutputStream::Initialize(fuchsia_audio_parameters* params,
                                   zx_time_t delay,
                                   AudioOutputDevice* device) {
  FXL_DCHECK(params->num_channels >= kMinNumChannels);
  FXL_DCHECK(params->num_channels <= kMaxNumChannels);
  FXL_DCHECK(params->sample_rate >= kMinSampleRate);
  FXL_DCHECK(params->sample_rate <= kMaxSampleRate);

  num_channels_ = params->num_channels;
  sample_rate_ = params->sample_rate;
  bytes_per_frame_ = num_channels_ * sizeof(int16_t);
  min_delay_nsec_ = delay;
  device_ = device;
  total_mapping_size_ = sample_rate_ * bytes_per_frame_;

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

  active_ = true;
  return true;
};

bool AudioOutputStream::AcquireRenderer() {
  media::AudioServerSyncPtr audio_server;
  app::ConnectToEnvironmentService(GetSynchronousProxy(&audio_server));

  // Only one of [AudioRenderer or MediaRenderer] must be kept open for playback
  media::AudioRendererSyncPtr audio_renderer;
  if (!audio_server->CreateRenderer(GetSynchronousProxy(&audio_renderer),
                                    GetSynchronousProxy(&media_renderer_))) {
    return false;
  }

  return media_renderer_->GetTimelineControlPoint(
      GetSynchronousProxy(&timeline_control_point_));
}

bool AudioOutputStream::SetMediaType(int num_channels, int sample_rate) {
  FXL_DCHECK(media_renderer_);

  auto details = media::AudioMediaTypeDetails::New();
  details->sample_format = media::AudioSampleFormat::SIGNED_16;
  details->channels = num_channels;
  details->frames_per_second = sample_rate;

  auto media_type = media::MediaType::New();
  media_type->medium = media::MediaTypeMedium::AUDIO;
  media_type->encoding = media::MediaType::kAudioEncodingLpcm;
  media_type->details = media::MediaTypeDetails::New();
  media_type->details->set_audio(std::move(details));

  if (!media_renderer_->SetMediaType(std::move(media_type))) {
    FXL_LOG(ERROR) << "Could not set media type";
    return false;
  }
  return true;
}

bool AudioOutputStream::CreateMemoryMapping() {
  zx_status_t status = zx::vmo::create(total_mapping_size_, 0, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmo::create failed - " << status;
    return false;
  }

  uintptr_t mapped_address;
  status = zx::vmar::root_self().map(
      0, vmo_, 0, total_mapping_size_,
      ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, &mapped_address);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_vmar_map failed - " << status;
    return false;
  }
  buffer_ = reinterpret_cast<int16_t*>(mapped_address);

  zx::vmo duplicate_vmo;
  status = vmo_.duplicate(
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_MAP,
      &duplicate_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::handle::duplicate failed - " << status;
    return false;
  }

  if (!media_renderer_->GetPacketConsumer(
          GetSynchronousProxy(&packet_consumer_))) {
    FXL_LOG(ERROR) << "PacketConsumer connection lost. Quitting.";
    return false;
  }

  FXL_DCHECK(packet_consumer_);
  if (!packet_consumer_->AddPayloadBuffer(kBufferId,
                                          std::move(duplicate_vmo))) {
    FXL_LOG(ERROR) << "Could not add payload buffer";
    return false;
  }

  return true;
}

void AudioOutputStream::FillBuffer(float* sample_buffer, int num_samples) {
  const float kAmplitudeScalar = std::numeric_limits<int16_t>::max();
  for (int idx = 0; idx < num_samples; ++idx) {
    // TODO(MTWN-44): Since we're passing int16 samples to the mixer, we need to
    // clamp potentially out-of-bounds values here to the specified range.
    float value = sample_buffer[idx];
    if (value < -1.0f) {
      value = -1.0f;
    } else if (value > 1.0f) {
      value = 1.0f;
    }
    buffer_[idx + current_sample_offset_] =
        static_cast<int16_t>(value * kAmplitudeScalar);
  }
}

media::MediaPacketPtr AudioOutputStream::CreateMediaPacket(
    zx_time_t pts,
    size_t payload_offset,
    size_t payload_size) {
  auto packet = media::MediaPacket::New();

  packet->pts_rate_ticks = sample_rate_;
  packet->pts_rate_seconds = 1;
  packet->keyframe = false;
  packet->end_of_stream = false;
  packet->payload_buffer_id = kBufferId;
  packet->payload_size = payload_size;
  packet->payload_offset = payload_offset;
  packet->pts = pts;

  return packet;
}

bool AudioOutputStream::SendMediaPacket(media::MediaPacketPtr packet) {
  FXL_DCHECK(packet_consumer_);

  return packet_consumer_->SupplyPacketNoDemand(std::move(packet));
}

int AudioOutputStream::GetMinDelay(zx_duration_t* delay_nsec_out) {
  FXL_DCHECK(delay_nsec_out);

  if (!active_) {
    return ZX_ERR_CONNECTION_ABORTED;
  }

  *delay_nsec_out = min_delay_nsec_;
  return ZX_OK;
}

int AudioOutputStream::Write(float* sample_buffer,
                             int num_samples,
                             zx_time_t pres_time) {
  FXL_DCHECK(sample_buffer);
  FXL_DCHECK(pres_time <= FUCHSIA_AUDIO_NO_TIMESTAMP);
  FXL_DCHECK(num_samples > 0);
  FXL_DCHECK(num_samples % num_channels_ == 0);

  if (!active_) {
    return ZX_ERR_CONNECTION_ABORTED;
  }

  if (pres_time == FUCHSIA_AUDIO_NO_TIMESTAMP && !received_first_frame_) {
    return ZX_ERR_BAD_STATE;
  }

  size_t num_frames = num_samples / num_channels_;
  FillBuffer(sample_buffer, num_samples);
  size_t buffer_offset = current_sample_offset_ * sizeof(int16_t);

  if ((current_sample_offset_ + num_samples) * sizeof(int16_t) >=
      total_mapping_size_) {
    current_sample_offset_ = 0;
  } else {
    current_sample_offset_ += num_samples;
  }

  zx_time_t subject_time = media::MediaPacket::kNoTimestamp;
  // On the first packet of a stream, we establish a time line with a start time
  // based on either the specified presentation time or a synthetic one we make
  // up if the caller didn't supply one. Packets past the first receive a PTS of
  // media::kNoTimestamp, meaning that they should be presented immediately
  // after the previous packet.
  if (!received_first_frame_) {
    if (pres_time == FUCHSIA_AUDIO_NO_TIMESTAMP) {
      start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC) + min_delay_nsec_ +
                    kSyntheticStartTimeAdjustment;
      subject_time = 0;
    } else {
      start_time_ = pres_time;
    }
  }

  if (!SendMediaPacket(CreateMediaPacket(subject_time, buffer_offset,
                                         num_frames * bytes_per_frame_))) {
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

  return ZX_OK;
}

bool AudioOutputStream::Start() {
  media::TimelineConsumerSyncPtr timeline_consumer;
  timeline_control_point_->GetTimelineConsumer(
      GetSynchronousProxy(&timeline_consumer));

  auto transform = media::TimelineTransform::New();
  transform->reference_time = start_time_;
  transform->subject_time = 0;
  transform->reference_delta = 1;
  transform->subject_delta = 1;

  bool completed;
  bool set =
      timeline_consumer->SetTimelineTransform(std::move(transform), &completed);
  return set && completed;
}

void AudioOutputStream::Stop() {
  received_first_frame_ = false;
  active_ = false;

  media::TimelineConsumerSyncPtr timeline_consumer;
  timeline_control_point_->GetTimelineConsumer(
      GetSynchronousProxy(&timeline_consumer));

  auto transform = media::TimelineTransform::New();
  transform->reference_time = media::kUnspecifiedTime;
  transform->subject_time = media::kUnspecifiedTime;
  transform->reference_delta = 1;
  transform->subject_delta = 0;

  bool completed;
  timeline_consumer->SetTimelineTransform(std::move(transform), &completed);
}

}  // namespace media_client
