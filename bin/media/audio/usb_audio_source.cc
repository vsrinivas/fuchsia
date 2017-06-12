// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/usb_audio_source.h"

#include <errno.h>
#include <fcntl.h>
#include <magenta/device/audio.h>
#include <mxio/io.h>

#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<UsbAudioSource> UsbAudioSource::Create(
    const std::string& device_path) {
  ftl::UniqueFD fd(open(device_path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FTL_LOG(ERROR) << "Failed to open audio device " << device_path;
    return nullptr;
  }

  return std::shared_ptr<UsbAudioSource>(new UsbAudioSource(std::move(fd)));
}

UsbAudioSource::UsbAudioSource(ftl::UniqueFD fd)
    : fd_(std::move(fd)),
      allocator_(PayloadAllocator::GetDefault()),
      state_(State::kStopped),
      read_buf_(kReadBufferSize) {
  ssize_t result = ioctl_audio_get_sample_rate(fd_.get(), &frames_per_second_);
  if (result != sizeof(frames_per_second_)) {
    FTL_LOG(ERROR) << "Failed to get sample rate from device, result " << result
                   << ", errno " << errno;
    frames_per_second_ = 0;
  }

  FTL_DLOG(INFO) << "Current sample rate (frames per second) "
                 << frames_per_second_;

  int frame_rate_count;
  result = ioctl_audio_get_sample_rate_count(fd_.get(), &frame_rate_count);
  if (result != sizeof(frame_rate_count)) {
    FTL_LOG(ERROR) << "Failed to get sample rate count from device, result "
                   << result << ", errno " << errno;
    frame_rate_count = 0;

    return;
  }

  frame_rates_.resize(frame_rate_count);

  result = ioctl_audio_get_sample_rates(fd_.get(), frame_rates_.data(),
                                        frame_rates_.size() * sizeof(uint32_t));
  if (result != static_cast<ssize_t>(frame_rates_.size() * sizeof(uint32_t))) {
    FTL_LOG(ERROR) << "Failed to get sample rates from device, result "
                   << result << ", errno " << errno;
    frame_rates_.clear();
  }

  for (uint32_t frame_rate : frame_rates_) {
    FTL_DLOG(INFO) << "Supported frame rate " << frame_rate;
  }
}

UsbAudioSource::~UsbAudioSource() {
  Stop();
}

std::vector<std::unique_ptr<media::StreamTypeSet>>
UsbAudioSource::GetSupportedStreamTypes() {
  std::vector<std::unique_ptr<media::StreamTypeSet>> result;

  for (uint32_t frame_rates : frame_rates_) {
    result.push_back(AudioStreamTypeSet::Create(
        {"lpcm"}, AudioStreamType::SampleFormat::kSigned16,
        Range<uint32_t>(2, 2), Range<uint32_t>(frame_rates, frame_rates)));
  }

  return result;
}

bool UsbAudioSource::SetStreamType(std::unique_ptr<StreamType> stream_type) {
  FTL_DCHECK(stream_type);
  if (state_ != State::kStopped) {
    FTL_LOG(ERROR) << "SetStreamType called after Start";
    return false;
  }

  if (stream_type->medium() != StreamType::Medium::kAudio ||
      stream_type->encoding() != "lpcm" ||
      stream_type->audio()->sample_format() !=
          AudioStreamType::SampleFormat::kSigned16 ||
      stream_type->audio()->channels() != 2) {
    FTL_LOG(ERROR) << "Unsupported stream type requested";
    return false;
  }

  bool frame_rate_valid = false;
  for (uint32_t frame_rate : frame_rates_) {
    if (stream_type->audio()->frames_per_second() == frame_rate) {
      frame_rate_valid = true;
      break;
    }
  }

  if (!frame_rate_valid) {
    FTL_LOG(ERROR) << "Unsupported stream type requested";
    return false;
  }

  return SetFrameRate(stream_type->audio()->frames_per_second());
}

void UsbAudioSource::Start() {
  if (state_ != State::kStopped) {
    return;
  }

  FTL_DCHECK(supply_callback_);

  state_ = State::kStarted;
  worker_thread_ = std::thread([this]() { Worker(); });
}

void UsbAudioSource::Stop() {
  if (state_ != State::kStarted) {
    return;
  }

  state_ = State::kStopping;

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  FTL_DCHECK(state_ == State::kStopped);
}

bool UsbAudioSource::can_accept_allocator() const {
  return true;
}

void UsbAudioSource::set_allocator(PayloadAllocator* allocator) {
  allocator_ = allocator;
}

void UsbAudioSource::SetSupplyCallback(const SupplyCallback& supply_callback) {
  supply_callback_ = supply_callback;
}

void UsbAudioSource::SetDownstreamDemand(Demand demand) {}

bool UsbAudioSource::SetFrameRate(uint32_t frames_per_second) {
  ssize_t result = ioctl_audio_set_sample_rate(fd_.get(), &frames_per_second);
  if (result != sizeof(frames_per_second)) {
    FTL_LOG(ERROR) << "Frame rate (" << frames_per_second
                   << "fps) rejected by device, result " << result << ", errno "
                   << errno;
    return false;
  }

  FTL_DLOG(INFO) << "Frame rate set to " << frames_per_second;

  frames_per_second_ = frames_per_second;
  pts_rate_ = TimelineRate(frames_per_second_, 1);

  return true;
}

void UsbAudioSource::Worker() {
  ssize_t result = ioctl_audio_start(fd_.get());
  if (result != MX_OK) {
    FTL_LOG(ERROR) << "Failed to start device, result " << result << ", errno "
                   << errno;
  }

  pts_ = 0;

  while (true) {
    if (state_ == State::kStopping) {
      break;
    }

    void* buf = allocator_->AllocatePayloadBuffer(packet_size());
    if (buf == nullptr) {
      FTL_LOG(ERROR) << "Allocator starved";
      state_ = State::kStopping;
      break;
    }

    // USB audio capture is particular about read sizes, rejecting small byte
    // counts. To work around this, we always ask for the same number of bytes
    // and use an intermediate buffer.
    // TODO(dalesat): Stop using the intermediate buffer.
    uint8_t* remaining_packet_buf = static_cast<uint8_t*>(buf);
    uint32_t remaining_packet_buf_byte_count_ = packet_size();

    while (remaining_packet_buf_byte_count_ != 0) {
      if (remaining_read_buf_byte_count_ == 0) {
        remaining_read_buf_ = read_buf_.data();
        ssize_t read_result =
            read(fd_.get(), remaining_read_buf_, read_buf_.size());
        FTL_DCHECK(read_result <= static_cast<ssize_t>(read_buf_.size()));

        if (read_result < 0) {
          FTL_LOG(ERROR) << "USB audio read failed, result " << read_result
                         << ", errno " << errno;
          state_ = State::kStopping;
          break;
        }

        remaining_read_buf_byte_count_ = static_cast<uint32_t>(read_result);
      }

      uint32_t copy_byte_count = std::min(remaining_packet_buf_byte_count_,
                                          remaining_read_buf_byte_count_);
      std::memcpy(remaining_packet_buf, remaining_read_buf_, copy_byte_count);

      remaining_read_buf_ += copy_byte_count;
      remaining_read_buf_byte_count_ -= copy_byte_count;

      remaining_packet_buf += copy_byte_count;
      remaining_packet_buf_byte_count_ -= copy_byte_count;
    }

    if (state_ != State::kStopping) {
      supply_callback_(Packet::Create(pts_, pts_rate_, false, false,
                                      packet_size(), buf, allocator_));

      pts_ += frames_per_packet();
    }
  }

  result = ioctl_audio_stop(fd_.get());
  if (result != MX_OK) {
    FTL_LOG(ERROR) << "Failed to stop device, result " << result << ", errno "
                   << errno;
  }

  state_ = State::kStopped;
}

}  // namespace media
