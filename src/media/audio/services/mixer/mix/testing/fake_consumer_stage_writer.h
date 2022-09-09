// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_CONSUMER_STAGE_WRITER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_CONSUMER_STAGE_WRITER_H_

#include <optional>
#include <vector>

#include "src/media/audio/services/mixer/mix/consumer_stage.h"

namespace media_audio {

// A very simple implementation that just records each Write call.
class FakeConsumerStageWriter : public ConsumerStage::Writer {
 public:
  // List of all written packets.
  struct Packet {
    bool is_silence;
    int64_t start_frame;
    int64_t length;
    void* data;
  };
  std::vector<Packet>& packets() { return packets_; }

  // List of all End calls. Each value is the `end_frame` of the last packet written before the End
  // call, or `std::nullopt` if there were no writers before the End call.
  std::vector<std::optional<int64_t>> end_calls() { return end_calls_; }

  // Implementation of ConsumerStage::Writer.
  void WriteData(int64_t start_frame, int64_t length, void* data) final {
    packets_.push_back({
        .is_silence = false,
        .start_frame = start_frame,
        .length = length,
        .data = data,
    });
    last_end_frame_ = start_frame + length;
    if (on_write_packet_) {
      on_write_packet_(start_frame, length, data);
    }
  }

  void WriteSilence(int64_t start_frame, int64_t length) final {
    packets_.push_back({
        .is_silence = true,
        .start_frame = start_frame,
        .length = length,
        .data = nullptr,
    });
    last_end_frame_ = start_frame + length;
    if (on_write_silence_) {
      on_write_silence_(start_frame, length);
    }
  }

  void End() final {
    end_calls_.push_back(last_end_frame_);
    if (on_end_) {
      on_end_();
    }
  }

  // Optional callbacks, to get notifications in addition to the saved packets.
  void SetOnWriteData(std::function<void(int64_t, int64_t, void*)> fn) {
    on_write_packet_ = std::move(fn);
  }
  void SetOnWriteSilence(std::function<void(int64_t, int64_t)> fn) {
    on_write_silence_ = std::move(fn);
  }
  void SetOnEnd(std::function<void()> fn) { on_end_ = std::move(fn); }

 private:
  std::optional<int64_t> last_end_frame_;
  std::vector<Packet> packets_;
  std::vector<std::optional<int64_t>> end_calls_;

  std::function<void(int64_t, int64_t, void*)> on_write_packet_;
  std::function<void(int64_t, int64_t)> on_write_silence_;
  std::function<void()> on_end_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_CONSUMER_STAGE_WRITER_H_
