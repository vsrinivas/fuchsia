// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/examples/simple_sine/simple_sine.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <math.h>

#include "src/lib/syslog/cpp/logger.h"

namespace {
// Set the AudioRenderer stream type to: 48 kHz, mono, 32-bit float.
constexpr float kFrameRate = 48000.0f;

// This example feeds the system 1 second of audio, in 10-millisecond payloads.
constexpr size_t kNumPayloads = 100;
constexpr size_t kFramesPerPayload = kFrameRate / kNumPayloads;

// Play a 439 Hz sine wave at 1/8 of full-scale volume.
constexpr double kFrequency = 439.0;
constexpr double kAmplitude = 0.125;
}  // namespace

namespace examples {

MediaApp::MediaApp(fit::closure quit_callback) : quit_callback_(std::move(quit_callback)) {
  FX_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data and start the presentation timeline
void MediaApp::Run(sys::ComponentContext* app_context) {
  AcquireAudioRenderer(app_context);
  SetStreamType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  WriteAudioIntoBuffer();
  for (size_t payload_num = 0; payload_num < kNumPayloads; ++payload_num) {
    SendPacket(CreatePacket(payload_num));
  }

  // By not explicitly setting timestamp values for reference clock or media
  // clock, we indicate that we want to start playback, with default timing.
  // I.e., at a system reference_time of "as soon as safely possible", we will
  // present audio corresponding to an initial media_time (PTS) of zero.
  //
  // AudioRenderer defaults to unity gain, unmuted; we need not change our
  // volume. (Although not shown here, we would do so via the GainControl
  // interface.)
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
}

// Use StartupContext to acquire AudioPtr, which we only need in order to get
// an AudioRendererPtr. Set an error handler, in case of channel closure.
void MediaApp::AcquireAudioRenderer(sys::ComponentContext* app_context) {
  fuchsia::media::AudioPtr audio = app_context->svc()->Connect<fuchsia::media::Audio>();

  audio->CreateAudioRenderer(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "fuchsia::media::AudioRenderer connection lost. Quitting.";
    Shutdown();
  });
}

// Set the AudioRenderer's audio stream_type: mono 48kHz 32-bit float.
void MediaApp::SetStreamType() {
  FX_DCHECK(audio_renderer_);

  fuchsia::media::AudioStreamType stream_type;

  stream_type.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  stream_type.channels = 1;
  stream_type.frames_per_second = kFrameRate;

  audio_renderer_->SetPcmStreamType(stream_type);
}

// Create a Virtual Memory Object, and map enough memory for audio buffers.
// Send a reduced-rights handle to AudioRenderer to act as a shared buffer.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx::vmo payload_vmo;

  payload_size_ = kFramesPerPayload * sizeof(float);
  total_mapping_size_ = payload_size_ * kNumPayloads;

  zx_status_t status =
      payload_buffer_.CreateAndMap(total_mapping_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                   &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "VmoMapper:::CreateAndMap failed - " << status;
    return status;
  }

  audio_renderer_->AddPayloadBuffer(0, std::move(payload_vmo));

  return ZX_OK;
}

// Write a sine wave into our buffer; we'll submit packets that point to it.
void MediaApp::WriteAudioIntoBuffer() {
  auto float_buffer = reinterpret_cast<float*>(payload_buffer_.start());

  for (size_t frame = 0; frame < kFramesPerPayload * kNumPayloads; ++frame) {
    float_buffer[frame] = kAmplitude * sin(frame * kFrequency * 2 * M_PI / kFrameRate);
  }
}

// We divide our cross-proc buffer into different zones, called payloads.
// Create a packet that corresponds to this particular payload.
// By specifying NO_TIMESTAMP for each packet's presentation timestamp, we rely
// on the AudioRenderer to treat the sequence of packets as a contiguous
// unbroken stream of audio. We just need to make sure we present packets early
// enough, and for this example we actually submit all packets before starting
// playback.
fuchsia::media::StreamPacket MediaApp::CreatePacket(size_t payload_num) {
  fuchsia::media::StreamPacket packet;

  // leave packet.pts as the default (fuchsia::media::NO_TIMESTAMP)
  // leave packet.payload_buffer_id as default (0): we only map a single buffer

  packet.payload_offset = (payload_num * payload_size_) % total_mapping_size_;
  packet.payload_size = payload_size_;
  return packet;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void MediaApp::SendPacket(fuchsia::media::StreamPacket packet) {
  ++num_packets_sent_;
  audio_renderer_->SendPacket(packet, [this]() { OnSendPacketComplete(); });
}

void MediaApp::OnSendPacketComplete() {
  ++num_packets_completed_;
  FX_DCHECK(num_packets_completed_ <= kNumPayloads);

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
  syslog::InitLogger({"simple_sine"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto startup_context = sys::ComponentContext::Create();

  examples::MediaApp media_app(
      [&loop]() { async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); }); });

  media_app.Run(startup_context.get());

  loop.Run();  // Now wait for the message loop to return...

  return 0;
}
