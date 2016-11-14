// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "apps/media/src/audio_server/platform/generic/standard_output_base.h"
#include "lib/ftl/files/unique_fd.h"

namespace media {
namespace audio {

class UsbOutput : public StandardOutputBase {
 public:
  static AudioOutputPtr Create(const std::string device_path,
                               AudioOutputManager* manager);

  ~UsbOutput();

  // AudioOutput implementation
  MediaResult Init() override;

  void Cleanup() override;

  // StandardOutputBase implementation
  bool StartMixJob(MixJob* job, ftl::TimePoint process_start) override;

  bool FinishMixJob(const MixJob& job) override;

 private:
  static constexpr uint32_t kFramesPerSecond = 48000;
  static constexpr uint32_t kChannels = 2;
  static constexpr uint32_t kBytesPerSample = 2;
  static constexpr AudioSampleFormat kSampleFormat =
      AudioSampleFormat::SIGNED_16;
  static constexpr uint32_t kMixesPerSecond = 100;

  UsbOutput(ftl::UniqueFD fd, AudioOutputManager* manager);

  ftl::UniqueFD fd_;
  bool started_ = false;
  uint32_t mix_buf_frames_;
  std::unique_ptr<uint8_t[]> mix_buf_;
  int64_t frames_sent_;
  TimelineFunction local_to_output_;
  uint32_t local_to_output_gen_ = 1;
};

}  // namespace audio
}  // namespace media
