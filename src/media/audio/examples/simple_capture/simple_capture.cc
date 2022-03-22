// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/examples/simple_capture/simple_capture.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/media/audio/cpp/types.h>

#include "src/media/audio/lib/logging/cli.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  media::examples::SimpleCapture simple_capture(
      [&loop]() { async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); }); });
  simple_capture.Run(component_context.get());
  loop.Run();

  return 0;
}

namespace media::examples {

// Connect to Audio service; create AudioCapturer; set capture format; create/map a VMO and send a
// duplicate handle as our payload buffer; open a .wav file for recording; start the capture stream.
void SimpleCapture::Run(sys::ComponentContext* app_context) {
  fuchsia::media::AudioPtr audio = app_context->svc()->Connect<fuchsia::media::Audio>();
  audio->CreateAudioCapturer(audio_capturer_.NewRequest(), kCaptureFromLoopback);
  audio_capturer_.set_error_handler([this](zx_status_t status) {
    printf("Client connection to fuchsia.media.AudioCapturer failed: %d\n", status);
    Shutdown();
  });

  audio_capturer_->SetPcmStreamType(
      media::CreateAudioStreamType(kSampleFormat, kCaptureChannels, kCaptureRate));

  zx::vmo mapped_vmo, duplicate_for_audio_capturer;
  auto status = vmo_mapper_.CreateAndMap(kBytesPerPayloadBuffer,
                                         /*map_flags=*/ZX_VM_PERM_READ, nullptr, &mapped_vmo,
                                         ZX_DEFAULT_VMO_RIGHTS);
  CLI_CHECK_OK(status,
               "Failed to create and map " << kBytesPerPayloadBuffer << "-byte payload buffer");
  status = mapped_vmo.duplicate(kPayloadBufferRights, &duplicate_for_audio_capturer);
  CLI_CHECK_OK(status, "Failed to duplicate VMO handle");
  audio_capturer_->AddPayloadBuffer(kPayloadBufferId, std::move(duplicate_for_audio_capturer));

  bool success = wav_writer_.Initialize(kCaptureFile, kSampleFormat, kCaptureChannels, kCaptureRate,
                                        kBytesPerSample * 8);
  CLI_CHECK(success, "Could not create file '" << kCaptureFile << "'");
  audio_capturer_.events().OnPacketProduced = [this](fuchsia::media::StreamPacket packet) {
    OnPacketProduced(packet);
  };
  audio_capturer_->StartAsyncCapture(kFramesPerPacket);

  printf("\nCapturing float32, %u Hz, %u-channel linear PCM, ", kCaptureRate, kCaptureChannels);
  printf("with %ld-frame packets (%ld msec) in a ", kFramesPerPacket, kPacketDuration.to_msecs());
  printf("%ld-byte (%ld-msec) payload buffer\n", kBytesPerPayloadBuffer,
         kPayloadBufferDuration.to_msecs());
  printf("from %s into '%s' ", kCaptureFromLoopback ? "loopback" : "default input", kCaptureFile);
  printf("for %ld frames (%ld msec).\n\n", kFramesToCapture, kCaptureFileDuration.to_msecs());
}

// A packet containing captured audio data was just returned to us -- handle it.
void SimpleCapture::OnPacketProduced(fuchsia::media::StreamPacket packet) {
  int64_t payload_size = packet.payload_size;

  if (frames_received_ + (payload_size / kBytesPerFrame) > kFramesToCapture) {
    payload_size = (kFramesToCapture - frames_received_) * kBytesPerFrame;
  }
  frames_received_ += (payload_size / kBytesPerFrame);

  if (payload_size && !wav_writer_.Write(reinterpret_cast<void* const>(
                                             reinterpret_cast<uint8_t*>(vmo_mapper_.start()) +
                                             packet.payload_offset),
                                         static_cast<uint32_t>(payload_size))) {
    printf("File write failed. Will try to retain any already-written data.\n");
    Shutdown();
    return;
  }

  // Each packet must be released, or eventually the capturer stops emitting.
  audio_capturer_->ReleasePacket(packet);

  // The most recent packet was enough; start the unwinding process (no need to wait for packets).
  if (frames_received_ >= kFramesToCapture) {
    audio_capturer_->StopAsyncCaptureNoReply();
    Shutdown();
  }
}

void SimpleCapture::Shutdown() {
  audio_capturer_.Unbind();
  if (wav_writer_.Close()) {
    printf("We recorded %zd frames.\n", frames_received_);
  } else {
    printf("File close failed.\n");
    CLI_CHECK(wav_writer_.Delete(), "Could not delete WAV file.");
  }
  quit_callback_();
}

}  // namespace media::examples
