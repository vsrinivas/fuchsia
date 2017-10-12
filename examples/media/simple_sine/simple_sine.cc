// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/simple_sine/simple_sine.h"

#include <zircon/syscalls.h>

#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/media/fidl/audio_server.fidl.h"

namespace {
// Set the renderer format to: 48 kHz, stereo, 16-bit LPCM (signed integer).
constexpr float kRendererFrameRate = 48000.0f;
constexpr size_t kNumChannels = 2;
constexpr size_t kSampleSize = sizeof(int16_t);
// For this example, feed audio to the system in payloads of 5 milliseconds.
constexpr size_t kMSecsPerPayload = 5;
constexpr size_t kFramesPerPayload =
    kMSecsPerPayload * kRendererFrameRate / 1000;
constexpr size_t kPayloadSize = kFramesPerPayload * kNumChannels * kSampleSize;
// Use 10 payload buffers, mapped contiguously in a single memory section (id 0)
constexpr size_t kNumPayloads = 10;
constexpr size_t kTotalMappingSize = kPayloadSize * kNumPayloads;
constexpr size_t kBufferId = 0;
// Play a sine wave that is 440 Hz, at 1/8 of full-scale volume.
constexpr float kFrequency = 440.0f;
constexpr float kFrequencyScalar = kFrequency * 2 * M_PI / kRendererFrameRate;
constexpr float kAmplitudeScalar = 0.125f * std::numeric_limits<int16_t>::max();
// Loop for 2 seconds.
constexpr size_t kTotalDurationSecs = 2;
constexpr size_t kNumPacketsToSend =
    kTotalDurationSecs * kRendererFrameRate / kFramesPerPayload;
}  // namespace

namespace examples {

MediaApp::MediaApp() {}
MediaApp::~MediaApp() {}

// Prepare for playback, submit initial data and start the presentation timeline
void MediaApp::Run(app::ApplicationContext* app_context) {
  AcquireRenderer(app_context);
  SetMediaType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  WriteStereoAudioIntoBuffer();
  for (size_t payload_num = 0; payload_num < kNumPayloads; ++payload_num) {
    SendMediaPacket(CreateMediaPacket(payload_num));
  }

  StartPlayback();
}

// Use ApplicationContext to acquire AudioServerPtr, MediaRendererPtr and
// PacketConsumerPtr in turn. Set error handlers, in case of channel closures.
void MediaApp::AcquireRenderer(app::ApplicationContext* app_context) {
  // AudioServer is needed only long enough to create the renderer(s).
  media::AudioServerPtr audio_server =
      app_context->ConnectToEnvironmentService<media::AudioServer>();

  // Only one of [AudioRenderer or MediaRenderer] must be kept open for playback
  media::AudioRendererPtr audio_renderer;
  audio_server->CreateRenderer(audio_renderer.NewRequest(),
                               media_renderer_.NewRequest());

  media_renderer_.set_connection_error_handler([this]() {
    FXL_LOG(ERROR) << "MediaRenderer connection lost. Quitting.";
    Shutdown();
  });
}

// Set the Mediarenderer's audio format to stereo 48kHz 16-bit (LPCM).
void MediaApp::SetMediaType() {
  FXL_DCHECK(media_renderer_);

  auto details = media::AudioMediaTypeDetails::New();
  details->sample_format = media::AudioSampleFormat::SIGNED_16;
  details->channels = kNumChannels;
  details->frames_per_second = kRendererFrameRate;

  auto media_type = media::MediaType::New();
  media_type->medium = media::MediaTypeMedium::AUDIO;
  media_type->encoding = media::MediaType::kAudioEncodingLpcm;
  media_type->details = media::MediaTypeDetails::New();
  media_type->details->set_audio(std::move(details));

  media_renderer_->SetMediaType(std::move(media_type));
}

// Create a single Virtual Memory Object, and map enough memory for our audio
// buffers. Open a PacketConsumer, and send it a duplicate handle of our VMO.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx_status_t status = zx::vmo::create(kTotalMappingSize, 0, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmo::create failed - " << status;
    return status;
  }

  status = zx::vmar::root_self().map(
      0, vmo_, 0, kTotalMappingSize,
      ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, &mapped_address_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_vmar_map failed - " << status;
    return status;
  }

  zx::vmo duplicate_vmo;
  status = vmo_.duplicate(
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_MAP,
      &duplicate_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::handle::duplicate failed - " << status;
    return status;
  }

  media_renderer_->GetPacketConsumer(packet_consumer_.NewRequest());

  packet_consumer_.set_connection_error_handler([this]() {
    FXL_LOG(ERROR) << "PacketConsumer connection lost. Quitting.";
    Shutdown();
  });

  FXL_DCHECK(packet_consumer_);
  packet_consumer_->AddPayloadBuffer(kBufferId, std::move(duplicate_vmo));

  return ZX_OK;
}

// Write a sine wave into our audio buffer. We'll continuously loop/resubmit it.
void MediaApp::WriteStereoAudioIntoBuffer() {
  int16_t* audio_buffer = reinterpret_cast<int16_t*>(mapped_address_);

  for (size_t frame = 0; frame < kFramesPerPayload * kNumPayloads; ++frame) {
    int16_t val = static_cast<int16_t>(round(
        kAmplitudeScalar * sin(static_cast<float>(frame) * kFrequencyScalar)));

    for (size_t chan_num = 0; chan_num < kNumChannels; ++chan_num) {
      audio_buffer[frame * kNumChannels + chan_num] = val;
    }
  }
}

// We divide the buffer into 10 payloads. Create a packet for this payload.
media::MediaPacketPtr MediaApp::CreateMediaPacket(size_t payload_num) {
  auto packet = media::MediaPacket::New();

  packet->pts_rate_ticks = kRendererFrameRate;
  packet->pts_rate_seconds = 1;
  packet->flags = 0;
  packet->payload_buffer_id = kBufferId;
  packet->payload_offset = (payload_num * kPayloadSize) % kTotalMappingSize;
  packet->payload_size = kPayloadSize;
  packet->pts = media::MediaPacket::kNoTimestamp;

  return packet;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if more packets remain, create and send the next packet;
// b. if no packets remain, begin closing down the system.
void MediaApp::SendMediaPacket(media::MediaPacketPtr packet) {
  FXL_DCHECK(packet_consumer_);

  packet_consumer_->SupplyPacket(
      std::move(packet), [this](media::MediaPacketDemandPtr) {
        ++num_packets_completed_;
        FXL_DCHECK(num_packets_completed_ <= kNumPacketsToSend);
        if (num_packets_completed_ + kNumPayloads <= kNumPacketsToSend) {
          SendMediaPacket(
              CreateMediaPacket(num_packets_completed_ + kNumPayloads));
        } else if (num_packets_completed_ >= kNumPacketsToSend) {
          Shutdown();
        }
      });
}

// Use TimelineConsumer (via TimelineControlPoint) to set playback rate to 1/1.
void MediaApp::StartPlayback() {
  media::MediaTimelineControlPointPtr timeline_control_point;
  media_renderer_->GetTimelineControlPoint(timeline_control_point.NewRequest());

  media::TimelineConsumerPtr timeline_consumer;
  timeline_control_point->GetTimelineConsumer(timeline_consumer.NewRequest());

  auto transform = media::TimelineTransform::New();
  transform->reference_time = media::kUnspecifiedTime;
  transform->subject_time = 0;
  transform->reference_delta = 1;
  transform->subject_delta = 1;

  timeline_consumer->SetTimelineTransformNoReply(std::move(transform));
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~MediaApp)
void MediaApp::Shutdown() {
  if (mapped_address_) {
    __UNUSED zx_status_t status =
        zx::vmar::root_self().unmap(mapped_address_, kTotalMappingSize);
    FXL_DCHECK(status == ZX_OK);
  }
  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

}  // namespace examples
