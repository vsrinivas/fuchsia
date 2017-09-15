// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <zircon/syscalls.h>
#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

#include "garnet/examples/media/simple_sine/simple_sine.h"

namespace {
// Set the renderer format to: stereo, 16-bit signed integer (LPCM), 48 kHz.
constexpr size_t kNumChannels = 2;
constexpr size_t kSampleSize = sizeof(int16_t);
constexpr float kRendererSampleRate = 48000.0f;
// Each buffer of audio payload will be 10 milliseconds in length.
constexpr size_t kFramesPerPayload = 480;
constexpr size_t kPayloadSize = kFramesPerPayload * kNumChannels * kSampleSize;
// Use 4 payload buffers, mapped contiguously in a single memory section (id 0).
constexpr size_t kNumPayloads = 4;
constexpr size_t kTotalMappingSize = kPayloadSize * kNumPayloads;
constexpr size_t kBufferId = 0;
// Play a sine wave that is 500 Hz, at 1/8 of full-scale.
constexpr float kFrequency = 500.0f;
constexpr float kFrequencyScalar = kFrequency * 2 * M_PI / kRendererSampleRate;
constexpr float kAmplitudeScalar = std::numeric_limits<int16_t>::max() * 0.125f;
// Loop for 1 second.
constexpr size_t kNumPacketsToSend = kRendererSampleRate / kFramesPerPayload;
}  // namespace

namespace examples {

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
  details->frames_per_second = kRendererSampleRate;

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

// We divide the buffer into 4 payloads. Create a packet for this payload.
media::MediaPacketPtr MediaApp::CreateMediaPacket(size_t payload_num) {
  auto packet = media::MediaPacket::New();

  packet->pts_rate_ticks = kRendererSampleRate;
  packet->pts_rate_seconds = 1;
  packet->keyframe = false;
  packet->end_of_stream = false;
  packet->payload_buffer_id = kBufferId;
  packet->payload_offset = payload_num * kPayloadSize;
  packet->payload_size = kPayloadSize;
  packet->pts = media::MediaPacket::kNoTimestamp;

  return packet;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if more packets remain, create and send the next packet;
// b. if no packets remain, begin closing down the system.
void MediaApp::SendMediaPacket(media::MediaPacketPtr packet) {
  FXL_DCHECK(packet_consumer_);

  ++num_packets_sent_;
  size_t payload_num = packet->payload_buffer_id;
  packet_consumer_->SupplyPacket(
      std::move(packet), [ this, packet_num = num_packets_sent_,
                           payload_num ](media::MediaPacketDemandPtr) {
        FXL_DCHECK(packet_num <= kNumPacketsToSend);
        if (packet_num <= kNumPacketsToSend - kNumPayloads) {
          SendMediaPacket(CreateMediaPacket(payload_num));
        } else if (packet_num >= kNumPacketsToSend) {
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

  timeline_consumer->SetTimelineTransform(std::move(transform), [](bool) {});
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
