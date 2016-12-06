// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "apps/media/src/framework/models/active_source.h"
#include "apps/media/src/framework/types/audio_stream_type.h"
#include "lib/ftl/files/unique_fd.h"

namespace media {
// USB audio input as an ActiveSource.
class UsbAudioSource : public ActiveSource {
 public:
  // Creates a usb audio input.
  static std::shared_ptr<UsbAudioSource> Create(const std::string& device_path);

  ~UsbAudioSource() override;

  std::vector<std::unique_ptr<media::StreamTypeSet>> GetSupportedStreamTypes();

  bool SetStreamType(std::unique_ptr<StreamType> stream_type);

  void Start();

  void Stop();

  // ActiveSource implementation
  bool can_accept_allocator() const override;

  void set_allocator(PayloadAllocator* allocator) override;

  void SetSupplyCallback(const SupplyCallback& supply_callback) override;

  void SetDownstreamDemand(Demand demand) override;

 private:
  static constexpr uint32_t kDefaultFrameRate = 48000;
  static constexpr uint32_t kChannels = 2;
  static constexpr uint32_t kBytesPerSample = 2;
  static constexpr AudioStreamType::SampleFormat kSampleFormat =
      AudioStreamType::SampleFormat::kSigned16;
  static constexpr uint32_t kPacketsPerSecond = 100;
  static constexpr uint32_t kReadBufferSize = 500;

  enum class State { kStopped, kStarted, kStopping };

  UsbAudioSource(ftl::UniqueFD fd);

  bool SetFrameRate(uint32_t frames_per_second);

  void Worker();

  uint32_t frames_per_packet() {
    return frames_per_second_ / kPacketsPerSecond;
  }

  uint32_t packet_size() {
    return frames_per_packet() * kChannels * kBytesPerSample;
  }

  // The fields below need to be stable while the worker thread is operating.
  ftl::UniqueFD fd_;
  std::vector<uint32_t> frame_rates_;
  uint32_t frames_per_second_;
  SupplyCallback supply_callback_;
  PayloadAllocator* allocator_;
  TimelineRate pts_rate_;
  // The fields above need to be stable while the worker thread is operating.

  std::atomic<State> state_;
  std::thread worker_thread_;

  // The fields below are accessed only by the worker thread.
  int64_t pts_;
  // TODO(dalesat): Stop using this intermediate buffer.
  std::vector<uint8_t> read_buf_;
  uint8_t* remaining_read_buf_;
  uint32_t remaining_read_buf_byte_count_ = 0;
  // The fields above are accessed only by the worker thread.
};

}  // namespace media
