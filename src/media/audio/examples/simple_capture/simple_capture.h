// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_SIMPLE_CAPTURE_SIMPLE_CAPTURE_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_SIMPLE_CAPTURE_SIMPLE_CAPTURE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>

#include "src/media/audio/lib/wav/wav_writer.h"

namespace media::examples {

class SimpleCapture {
  // Constants that configure our audio capture.
  static constexpr bool kCaptureFromLoopback = false;
  static constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::FLOAT;
  static constexpr auto kBytesPerSample = sizeof(float);
  static constexpr auto kCaptureRate = 48000;
  static constexpr auto kCaptureChannels = 1;
  static constexpr auto kPacketDuration = zx::msec(25);
  static constexpr auto kPayloadBufferDuration = zx::msec(500);
  static constexpr auto kCaptureFileDuration = zx::sec(2);
  static constexpr char kCaptureFile[] = "/tmp/simple_capture.wav";

  // Other constants, some derived from the above.
  static constexpr auto kFramesPerPacket =
      (kCaptureRate * kPacketDuration.get()) / zx::sec(1).get();
  static constexpr auto kFramesToCapture =
      (kCaptureRate * kCaptureFileDuration.get()) / zx::sec(1).get();
  static constexpr auto kBytesPerFrame = kBytesPerSample * kCaptureChannels;
  static constexpr auto kBytesPerPayloadBuffer =
      kFramesPerPacket * (kPayloadBufferDuration / kPacketDuration) * kBytesPerFrame;
  static constexpr auto kPayloadBufferId = 0;
  static constexpr auto kPayloadBufferRights =
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP;

 public:
  explicit SimpleCapture(fit::closure quit_callback) : quit_callback_(std::move(quit_callback)) {}
  ~SimpleCapture() = default;
  void Run(sys::ComponentContext* app_context);

 private:
  void SetupPayloadBuffer();
  void DisplaySummary();
  void OnPacketProduced(fuchsia::media::StreamPacket pkt);
  void Shutdown();

  fuchsia::media::AudioCapturerPtr audio_capturer_;
  media::audio::WavWriter<> wav_writer_;
  fzl::VmoMapper vmo_mapper_;

  int64_t frames_received_ = 0;
  fit::closure quit_callback_;
};

}  // namespace media::examples

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_SIMPLE_CAPTURE_SIMPLE_CAPTURE_H_
