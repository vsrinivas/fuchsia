// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/platform/usb/usb_output.h"

#include <fcntl.h>
#include <magenta/device/audio.h>
#include <mxio/io.h>

#include "apps/media/src/audio_server/audio_output_manager.h"
#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/logging.h"

namespace media {
namespace audio {

// static
AudioOutputPtr UsbOutput::Create(ftl::UniqueFD dev_node,
                                 AudioOutputManager* manager) {
  return AudioOutputPtr(new UsbOutput(std::move(dev_node), manager));
}

UsbOutput::UsbOutput(ftl::UniqueFD dev_node, AudioOutputManager* manager)
    : StandardOutputBase(manager), fd_(std::move(dev_node)) {
  FTL_DCHECK(fd_.is_valid());
}

UsbOutput::~UsbOutput() {}

MediaResult UsbOutput::Init() {
  uint32_t frames_per_second = kFramesPerSecond;
  int result = ioctl_audio_set_sample_rate(fd_.get(), &frames_per_second);
  if (result != MX_OK) {
    FTL_LOG(ERROR) << "Sample rate (" << kFramesPerSecond
                   << "fps) not supported";
    return MediaResult::UNSUPPORTED_CONFIG;
  }

  AudioMediaTypeDetailsPtr config(AudioMediaTypeDetails::New());
  config->frames_per_second = kFramesPerSecond;
  config->channels = kChannels;
  config->sample_format = kSampleFormat;

  output_formatter_ = OutputFormatter::Select(config);
  if (!output_formatter_) {
    return MediaResult::UNSUPPORTED_CONFIG;
  }

  mix_buf_frames_ = kFramesPerSecond / kMixesPerSecond;

  size_t buffer_size = mix_buf_frames_ * output_formatter_->bytes_per_frame();
  mix_buf_.reset(new uint8_t[buffer_size]);

  frames_sent_ = 0;

  // Set up the intermediate buffer at the StandardOutputBase level
  SetupMixBuffer(mix_buf_frames_);

  // For now, USB devices are considered to be plugged at the time their device
  // node shows up.
  UpdatePlugState(true, mx_time_get(MX_CLOCK_MONOTONIC));

  return MediaResult::OK;
}

void UsbOutput::Cleanup() {
  if (started_) {
    ioctl_audio_stop(fd_.get());
  }
}

bool UsbOutput::StartMixJob(MixJob* job, ftl::TimePoint process_start) {
  if (!started_) {
    ioctl_audio_start(fd_.get());
    started_ = true;
    local_to_output_ = TimelineFunction(
        ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds(), frames_sent_,
        Timeline::ns_from_seconds(1), kFramesPerSecond);

    // Prime the device and come back in a millisecond.
    output_formatter_->FillWithSilence(mix_buf_.get(), mix_buf_frames_);
    write(fd_.get(), mix_buf_.get(),
          mix_buf_frames_ * kChannels * kBytesPerSample);
    write(fd_.get(), mix_buf_.get(),
          mix_buf_frames_ * kChannels * kBytesPerSample);
    SetNextSchedTime(process_start + ftl::TimeDelta::FromMilliseconds(1));
    return false;
  }

  job->buf = mix_buf_.get();
  job->buf_frames = mix_buf_frames_;
  job->start_pts_of = frames_sent_;
  job->local_to_output = &local_to_output_;
  job->local_to_output_gen = local_to_output_gen_;

  SetNextSchedTime(process_start + ftl::TimeDelta::FromMilliseconds(1));

  return true;
}

bool UsbOutput::FinishMixJob(const MixJob& job) {
  int write_size = job.buf_frames * kChannels * kBytesPerSample;
  int write_result = write(fd_.get(), job.buf, write_size);
  // TODO(dalesat): Refine local_to_output_.
  if (write_result < write_size) {
    return false;
  }

  frames_sent_ += job.buf_frames;

  return false;
}

}  // namespace audio
}  // namespace media
