// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_TOOLS_WAV_RECORDER_WAV_RECORDER_H_
#define SRC_MEDIA_AUDIO_TOOLS_WAV_RECORDER_WAV_RECORDER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/ultrasound/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/command_line.h"
#include "src/media/audio/lib/logging/cli.h"
#include "src/media/audio/lib/wav/wav_writer.h"

namespace {
typedef enum { Default, Flexible, Monotonic, Custom } ClockType;
}  // namespace

namespace media::tools {

class WavRecorder {
  static constexpr float kDefaultCaptureGainDb = 0.0f;

  static constexpr float kDefaultFileDurationSecs = 2.0f;
  static constexpr float kMaxFileDurationSecs = 86400.0f;

  static constexpr zx_duration_t kDefaultPacketDuration = ZX_MSEC(100);
  static constexpr float kMinPacketSizeMsec = 1.0f;  // minimum packet size 1 msec!
  static constexpr float kMaxPacketSizeMsec = 500.0f;

 public:
  WavRecorder(fxl::CommandLine cmd_line, fit::closure quit_callback)
      : cmd_line_(std::move(cmd_line)), quit_callback_(std::move(quit_callback)) {
    CLI_CHECK(quit_callback_, "quit_callback cannot be null");
  }

  ~WavRecorder();
  void Run(sys::ComponentContext* app_context);

 private:
  void Usage();

  void SetupPayloadBuffer();
  void EstablishReferenceClock();
  void ReceiveClockAndContinue(
      zx::clock received_clock,
      std::optional<fuchsia::media::AudioStreamType> stream_type = std::nullopt);
  void OnDefaultFormatFetched(const fuchsia::media::AudioStreamType& fmt);

  void SendCaptureJob();
  void OnPacketProduced(fuchsia::media::StreamPacket pkt);
  void TimeToStr(int64_t time, char* time_str);
  void DisplayPacket(fuchsia::media::StreamPacket pkt);

  void OnQuit();
  bool Shutdown();

  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::audio::GainControlPtr gain_control_;
  fsl::FDWaiter keystroke_waiter_;
  media::audio::WavWriter<> wav_writer_;
  bool wav_writer_initialized_ = false;

  fxl::CommandLine cmd_line_;
  fit::closure quit_callback_;
  const char* filename_ = "";
  bool verbose_ = false;
  bool loopback_ = false;

  zx::clock reference_clock_;
  ClockType clock_type_ = ClockType::Default;
  bool adjusting_clock_rate_ = false;
  int32_t clock_rate_adjustment_;

  zx::vmo payload_buf_vmo_;
  void* payload_buf_virt_ = nullptr;
  uint32_t payload_buf_size_ = 0;
  uint32_t payload_buf_frames_ = 0;
  std::unique_ptr<uint8_t[]> compress_32_24_buff_;  // only used for 'packed-24'
  bool pack_24bit_samples_ = false;

  fuchsia::media::AudioSampleFormat sample_format_;
  float stream_gain_db_ = kDefaultCaptureGainDb;
  bool stream_mute_ = false;
  uint32_t channel_count_ = 0;
  uint32_t frames_per_second_ = 0;
  uint32_t bytes_per_frame_ = 0;
  zx_duration_t packet_duration_ = kDefaultPacketDuration;

  bool file_duration_specifed_ = false;
  zx::duration file_duration_;

  uint32_t frames_per_packet_ = 0;
  uint32_t packets_per_payload_buf_ = 0;
  uint32_t payload_buf_frame_offset_ = 0;
  bool clean_shutdown_ = false;
  uint32_t outstanding_capture_jobs_ = 0;

  bool ultrasound_ = false;
  fuchsia::ultrasound::FactoryPtr ultrasound_factory_;
};

}  // namespace media::tools

#endif  // SRC_MEDIA_AUDIO_TOOLS_WAV_RECORDER_WAV_RECORDER_H_
