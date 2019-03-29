// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_WAV_RECORDER_WAV_RECORDER_H_
#define GARNET_BIN_MEDIA_WAV_RECORDER_WAV_RECORDER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/lib/media/wav_writer/wav_writer.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"

namespace media::tools {

class WavRecorder {
 public:
  WavRecorder(fxl::CommandLine cmd_line, fit::closure quit_callback)
      : cmd_line_(std::move(cmd_line)),
        quit_callback_(std::move(quit_callback)) {
    FXL_DCHECK(quit_callback_);
  }

  ~WavRecorder();
  void Run(component::StartupContext* app_context);

 private:
  void Usage();
  void Shutdown();
  bool SetupPayloadBuffer();
  void SendCaptureJob();
  void OnDefaultFormatFetched(fuchsia::media::StreamType type);
  void OnPacketProduced(fuchsia::media::StreamPacket pkt);
  void OnQuit();

  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::audio::GainControlPtr gain_control_;
  fsl::FDWaiter keystroke_waiter_;
  media::audio::WavWriter<> wav_writer_;

  fxl::CommandLine cmd_line_;
  fit::closure quit_callback_;
  const char* filename_ = "";
  bool verbose_ = false;
  bool loopback_ = false;

  zx::vmo payload_buf_vmo_;
  void* payload_buf_virt_ = nullptr;
  size_t payload_buf_size_ = 0;
  size_t payload_buf_frames_ = 0;
  std::unique_ptr<uint8_t[]> compress_32_24_buff_;  // only used for 'packed-24'
  bool pack_24bit_samples_ = false;

  fuchsia::media::AudioSampleFormat sample_format_;
  float stream_gain_db_ = 0.0f;
  bool stream_mute_ = false;
  uint32_t channel_count_ = 0;
  uint32_t frames_per_second_ = 0;
  uint32_t bytes_per_frame_ = 0;
  size_t capture_frames_per_chunk_ = 0;
  size_t capture_frame_offset_ = 0;
  bool clean_shutdown_ = false;
  uint32_t outstanding_capture_jobs_ = 0;
};

}  // namespace media::tools

#endif  // GARNET_BIN_MEDIA_WAV_RECORDER_WAV_RECORDER_H_
