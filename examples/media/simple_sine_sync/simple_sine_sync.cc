// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/simple_sine_sync/simple_sine_sync.h"

#include <zircon/syscalls.h>

#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
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
// Use a single memory section (id 0) that can contain 1 second of our audio.
constexpr size_t kBufferId = 0;
constexpr size_t kTotalMappingFrames = kRendererFrameRate;
constexpr size_t kTotalMappingSize =
    kTotalMappingFrames * kNumChannels * kSampleSize;
constexpr size_t kNumPayloads = kTotalMappingSize / kPayloadSize;
// Play a sine wave that is 439 Hz, at 1/8 of full-scale volume.
constexpr float kFrequency = 439.0f;
constexpr float kAmplitudeScalar = 0.125f * std::numeric_limits<int16_t>::max();
constexpr float kFrequencyScalar = kFrequency * 2 * M_PI / kRendererFrameRate;
// Loop for 2 seconds.
constexpr size_t kTotalDurationSecs = 2;
constexpr size_t kNumPacketsToSend =
    kTotalDurationSecs * kRendererFrameRate / kFramesPerPayload;

}  // namespace

namespace examples {

MediaApp::MediaApp() {}
MediaApp::~MediaApp() {}

// Prepare for playback, compute playback data, supply media packets, start.
int MediaApp::Run() {
  if (verbose_) {
    printf("First PTS delay: %ldms\n", first_pts_delay_ / 1000000);
    printf("Low water mark: %ldms\n", low_water_mark_ / 1000000);
    printf("High water mark: %ldms\n", high_water_mark_ / 1000000);
  }
  if (!AcquireRenderer()) {
    FXL_LOG(ERROR) << "Could not acquire renderer";
    return 1;
  }

  SetMediaType();

  if (CreateMemoryMapping() != ZX_OK) {
    return 2;
  }

  WriteStereoAudioIntoBuffer(mapped_address_, kTotalMappingFrames);

  start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC) + first_pts_delay_;
  RefillBuffer();
  if (!StartPlayback(start_time_))
    return 3;

  while (num_packets_sent_ < kNumPacketsToSend) {
    WaitForPackets(num_packets_sent_);
    RefillBuffer();
  }
  WaitForPackets(kNumPacketsToSend);  // Wait for last packet to complete

  return 0;
}

// Connect to AudioServer and MediaRenderer services.
bool MediaApp::AcquireRenderer() {
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

  if (!media_renderer_->SetMediaType(std::move(media_type)))
    FXL_LOG(ERROR) << "Could not set media type";
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

  if (!media_renderer_->GetPacketConsumer(
          GetSynchronousProxy(&packet_consumer_))) {
    FXL_LOG(ERROR) << "PacketConsumer connection lost. Quitting.";
    return ZX_ERR_UNAVAILABLE;
  }

  FXL_DCHECK(packet_consumer_);
  if (!packet_consumer_->AddPayloadBuffer(kBufferId,
                                          std::move(duplicate_vmo))) {
    FXL_LOG(ERROR) << "Could not add payload buffer";
    return ZX_ERR_UNAVAILABLE;
  }

  return ZX_OK;
}

// Write a sine wave into our audio buffer. We'll continuously loop/resubmit it.
void MediaApp::WriteStereoAudioIntoBuffer(uintptr_t buffer, size_t num_frames) {
  int16_t* audio_buffer = reinterpret_cast<int16_t*>(buffer);

  for (size_t frame = 0; frame < num_frames; ++frame) {
    int16_t val = static_cast<int16_t>(round(
        kAmplitudeScalar * sin(static_cast<float>(frame) * kFrequencyScalar)));
    for (size_t chan_num = 0; chan_num < kNumChannels; ++chan_num) {
      audio_buffer[frame * kNumChannels + chan_num] = val;
    }
  }
}

// Create a packet for this payload.
media::MediaPacketPtr MediaApp::CreateMediaPacket(size_t payload_num) {
  auto packet = media::MediaPacket::New();

  packet->pts_rate_ticks = kRendererFrameRate;
  packet->pts_rate_seconds = 1;
  packet->pts = (payload_num == 0 ? 0 : media::MediaPacket::kNoTimestamp);
  packet->keyframe = false;
  packet->end_of_stream = false;
  packet->payload_buffer_id = kBufferId;
  packet->payload_size = kPayloadSize;
  packet->payload_offset = (payload_num % kNumPayloads) * kPayloadSize;

  return packet;
}

// Submit a packet, incrementing our count of packets sent.
bool MediaApp::SendMediaPacket(media::MediaPacketPtr packet) {
  FXL_DCHECK(packet_consumer_);

  if (verbose_) {
    const float delay = (float)zx_time_get(ZX_CLOCK_MONOTONIC) - start_time_;
    printf("SendMediaPacket num %zu time %.2f\n", num_packets_sent_,
           num_packets_sent_ ? delay / 1000000 : (float)-first_pts_delay_);
  }

  ++num_packets_sent_;
  // Note: SupplyPacketNoReply returns immediately, before packet is consumed.
  return packet_consumer_->SupplyPacketNoReply(std::move(packet));
}

// Stay ahead of the presentation timeline, by the amount high_water_mark_.
// We must wait until a packet is consumed before reusing its buffer space.
// For more fine-grained awareness/control of buffers, clients should use the
// (asynchronous) MediaPacketConsumerPtr interface and call SupplyPacket().
bool MediaApp::RefillBuffer() {
  const zx_duration_t now = zx_time_get(ZX_CLOCK_MONOTONIC);
  const zx_duration_t time_data_needed =
      now - std::min(now, start_time_) + high_water_mark_;
  size_t num_payloads_needed =
      ceil((float)time_data_needed / ZX_MSEC(kMSecsPerPayload));
  num_payloads_needed = std::min(kNumPacketsToSend, num_payloads_needed);

  if (verbose_) {
    printf("RefillBuffer  now: %.3f start: %.3f :: need %lu (%.4f), sent %lu\n",
           (float)now / 1000000, (float)start_time_ / 1000000,
           num_payloads_needed * kMSecsPerPayload,
           (float)time_data_needed / 1000000,
           num_packets_sent_ * kMSecsPerPayload);
  }
  while (num_packets_sent_ < num_payloads_needed) {
    if (!SendMediaPacket(CreateMediaPacket(num_packets_sent_))) {
      return false;
    }
  }

  return true;
}

// Use TimelineConsumer (via TimelineControlPoint) to set playback rate.
bool MediaApp::StartPlayback(uint64_t reference_time) {
  media::TimelineConsumerSyncPtr timeline_consumer;
  timeline_control_point_->GetTimelineConsumer(
      GetSynchronousProxy(&timeline_consumer));

  auto transform = media::TimelineTransform::New();
  transform->reference_time = reference_time;
  transform->subject_time = 0;
  transform->reference_delta = 1;
  transform->subject_delta = 1;

  const zx_time_t before = zx_time_get(ZX_CLOCK_MONOTONIC);
  bool status =
      timeline_consumer->SetTimelineTransformNoReply(std::move(transform));
  if (verbose_) {
    printf("SetTimelineTransform(%.3fms) at %.3fms took %.2fms, returned %d\n",
           (float)reference_time / 1000000, (float)before / 1000000,
           (float)(zx_time_get(ZX_CLOCK_MONOTONIC) - before) / 1000000, status);
  }
  return status;
}

void MediaApp::WaitForPackets(size_t num_packets) {
  const zx_duration_t audio_submitted = ZX_MSEC(kMSecsPerPayload) * num_packets;
  zx_time_t wake_time = start_time_ + audio_submitted - low_water_mark_;
  if (num_packets >= kNumPacketsToSend)
    wake_time += (low_water_mark_ - first_pts_delay_);

  const zx_time_t now = zx_time_get(ZX_CLOCK_MONOTONIC);
  if (wake_time > now) {
    if (verbose_) {
      const zx_duration_t nap_duration = wake_time - now;
      printf("sleeping for %.05f ms\n", (double)nap_duration / 1000000);
    }
    zx_nanosleep(wake_time);
  }
}

}  // namespace examples
