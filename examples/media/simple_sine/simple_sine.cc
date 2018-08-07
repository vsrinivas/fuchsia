// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/simple_sine/simple_sine.h"

#include "lib/async-loop/cpp/loop.h"
#include "lib/fxl/logging.h"

namespace {
// This example feeds the system 1 second of audio, in 10-millisecond payloads.
constexpr size_t kNumPayloads = 100;
// Set the renderer stream type to: 48 kHz, mono, 32-bit float.
constexpr float kRendererFrameRate = 48000.0f;
constexpr size_t kFramesPerPayload = kRendererFrameRate / kNumPayloads;

// Play a 439 Hz sine wave at 1/8 of full-scale volume.
constexpr double kFrequency = 439.0;
constexpr double kAmplitude = 0.125;
}  // namespace

namespace examples {

MediaApp::MediaApp(fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data and start the presentation timeline
void MediaApp::Run(component::StartupContext* app_context) {
  AcquireRenderer(app_context);
  SetStreamType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  WriteAudioIntoBuffer();
  for (size_t payload_num = 0; payload_num < kNumPayloads; ++payload_num) {
    SendPacket(CreatePacket(payload_num));
  }

  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                               fuchsia::media::NO_TIMESTAMP);
}

// Use StartupContext to acquire AudioPtr, which we only need in order to get
// an AudioRendererPtr. Set an error handler, in case of channel closure.
void MediaApp::AcquireRenderer(component::StartupContext* app_context) {
  fuchsia::media::AudioPtr audio =
      app_context->ConnectToEnvironmentService<fuchsia::media::Audio>();

  audio->CreateAudioOut(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this]() {
    FXL_LOG(ERROR)
        << "fuchsia::media::AudioRenderer connection lost. Quitting.";
    Shutdown();
  });
}

// Set the renderer's audio stream_type: mono 48kHz 32-bit float.
void MediaApp::SetStreamType() {
  FXL_DCHECK(audio_renderer_);

  fuchsia::media::AudioStreamType stream_type;

  stream_type.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  stream_type.channels = 1;
  stream_type.frames_per_second = kRendererFrameRate;

  audio_renderer_->SetPcmStreamType(std::move(stream_type));
}

// Create a Virtual Memory Object, and map enough memory for audio buffers.
// Send a reduced-rights handle to AudioRenderer to act as a shared buffer.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx::vmo payload_vmo;

  payload_size_ = kFramesPerPayload * sizeof(float);
  total_mapping_size_ = payload_size_ * kNumPayloads;

  zx_status_t status = payload_buffer_.CreateAndMap(
      total_mapping_size_, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      nullptr, &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VmoMapper:::CreateAndMap failed - " << status;
    return status;
  }

  audio_renderer_->AddPayloadBuffer(0, std::move(payload_vmo));

  return ZX_OK;
}

// Write a sine wave into our buffer; we'll submit packets that point to it.
void MediaApp::WriteAudioIntoBuffer() {
  float* float_buffer = reinterpret_cast<float*>(payload_buffer_.start());

  for (size_t frame = 0; frame < kFramesPerPayload * kNumPayloads; ++frame) {
    float_buffer[frame] =
        kAmplitude * sin(frame * kFrequency * 2 * M_PI / kRendererFrameRate);
  }
}

// We divide our cross-proc buffer into different zones, called payloads.
// Create a packet that corresponds to this particular payload.
fuchsia::media::StreamPacket MediaApp::CreatePacket(size_t payload_num) {
  fuchsia::media::StreamPacket packet;
  packet.payload_offset = (payload_num * payload_size_) % total_mapping_size_;
  packet.payload_size = payload_size_;
  return packet;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void MediaApp::SendPacket(fuchsia::media::StreamPacket packet) {
  ++num_packets_sent_;
  audio_renderer_->SendPacket(std::move(packet),
                              [this]() { OnSendPacketComplete(); });
}

void MediaApp::OnSendPacketComplete() {
  ++num_packets_completed_;
  FXL_DCHECK(num_packets_completed_ <= kNumPayloads);

  if (num_packets_sent_ < kNumPayloads) {
    SendPacket(CreatePacket(num_packets_sent_));
  } else if (num_packets_completed_ >= kNumPayloads) {
    Shutdown();
  }
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~MediaApp).
void MediaApp::Shutdown() {
  payload_buffer_.Unmap();
  quit_callback_();
}

}  // namespace examples

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  examples::MediaApp media_app([&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  media_app.Run(startup_context.get());

  loop.Run();  // Now wait for the message loop to return...

  return 0;
}
