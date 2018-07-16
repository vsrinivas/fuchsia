// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_WAV_RECORD_WAV_RECORDER_H_
#define GARNET_EXAMPLES_MEDIA_WAV_RECORD_WAV_RECORDER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/lib/media/wav_writer/wav_writer.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"

namespace examples {

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
  void OnDefaultFormatFetched(fuchsia::media::MediaType type);
  void OnPacketCaptured(fuchsia::media::MediaPacket pkt);
  void OnQuit();

  fuchsia::media::AudioCapturerPtr capturer_;
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

  fuchsia::media::AudioSampleFormat sample_format_;
  uint32_t channel_count_ = 0;
  uint32_t frames_per_second_ = 0;
  uint32_t bytes_per_frame_ = 0;
  size_t capture_frames_per_chunk_ = 0;
  size_t capture_frame_offset_ = 0;
  bool clean_shutdown_ = false;
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_MEDIA_WAV_RECORD_WAV_RECORDER_H_
