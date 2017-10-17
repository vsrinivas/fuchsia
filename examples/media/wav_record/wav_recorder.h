// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/media/fidl/audio_capturer.fidl.h"
#include "lib/media/fidl/audio_server.fidl.h"

namespace examples {

class WavRecorder : public media::AudioCapturerClient {
 public:
  WavRecorder(fxl::CommandLine cmd_line)
      : async_binding_(this), cmd_line_(std::move(cmd_line)) {}
  ~WavRecorder();
  void Run(app::ApplicationContext* app_context);

 private:
  void Usage();
  void Shutdown();
  bool SetupPayloadBuffer();
  void SendCaptureJob();
  void OnDefaultFormatFetched(media::MediaTypePtr type);
  void OnPacketCaptured(media::MediaPacketPtr pkt) override;
  void OnQuit();

  media::AudioCapturerPtr capturer_;
  fidl::Binding<AudioCapturerClient> async_binding_;
  fsl::FDWaiter keystroke_waiter_;

  fxl::CommandLine cmd_line_;
  fxl::UniqueFD wav_file_;
  const char* filename_ = "<unknown>";
  bool verbose_ = false;
  bool loopback_ = false;

  zx::vmo payload_buf_vmo_;
  void* payload_buf_virt_ = nullptr;
  size_t payload_buf_size_ = 0;
  size_t payload_buf_frames_ = 0;

  media::AudioSampleFormat sample_format_;
  uint32_t channel_count_ = 0;
  uint32_t frames_per_second_ = 0;
  uint32_t bytes_per_frame_ = 0;
  size_t capture_frames_per_chunk_ = 0;
  size_t capture_frame_offset_ = 0;
  size_t payload_written_ = 0;
  bool clean_shutdown_ = false;
};

}  // namespace examples
