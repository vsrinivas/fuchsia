// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <zircon/device/audio.h>
#include <zircon/types.h>
#include <fbl/unique_ptr.h>

#include "garnet/bin/media/framework/models/active_source.h"
#include "garnet/bin/media/framework/types/audio_stream_type.h"

namespace audio {
namespace utils {
class AudioInput;
}  // namespace utils
}  // namespace audio

namespace media {

// Audio input as an ActiveSource.
class AudioInput : public ActiveSource {
 public:
  // Creates a usb audio input.
  static std::shared_ptr<AudioInput> Create(const std::string& device_path);

  ~AudioInput() override;

  std::vector<std::unique_ptr<media::StreamTypeSet>> GetSupportedStreamTypes();

  bool SetStreamType(std::unique_ptr<StreamType> stream_type);

  void Start();

  void Stop();

  // ActiveSource implementation
  bool can_accept_allocator() const override;

  void set_allocator(PayloadAllocator* allocator) override;

  void SetDownstreamDemand(Demand demand) override;

 private:
  static constexpr uint32_t kPacketsPerRingBuffer = 16;
  static constexpr uint32_t kPacketsPerSecond = 100;

  enum class State { kUninitialized, kStopped, kStarted, kStopping };

  AudioInput(const std::string& device_path);
  zx_status_t Initalize();

  void Worker();

  uint32_t frames_per_packet() const {
    return configured_frames_per_second_ / kPacketsPerSecond;
  }

  uint32_t packet_size() const {
    return frames_per_packet() * configured_bytes_per_frame_;
  }

  // The fields below need to be stable while the worker thread is operating.
  fbl::unique_ptr<audio::utils::AudioInput> audio_input_;
  std::vector<std::unique_ptr<media::StreamTypeSet>> supported_stream_types_;
  bool config_valid_ = false;
  uint32_t configured_frames_per_second_;
  uint16_t configured_channels_;
  audio_sample_format_t configured_sample_format_;
  uint32_t configured_bytes_per_frame_;
  PayloadAllocator* allocator_;
  TimelineRate pts_rate_;
  // The fields above need to be stable while the worker thread is operating.

  std::atomic<State> state_;
  std::thread worker_thread_;
};

}  // namespace media
