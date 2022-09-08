// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_CONSUMER_STAGE_WRITER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_CONSUMER_STAGE_WRITER_H_

#include <vector>

#include "src/media/audio/services/mixer/mix/consumer_stage.h"

namespace media_audio {

// A very simple implementation that just records each Write call.
class FakeConsumerStageWriter : public ConsumerStage::Writer {
 public:
  struct Packet {
    bool is_silence;
    int64_t start_frame;
    int64_t length;
    void* payload;
  };
  std::vector<Packet>& packets() { return packets_; }

  // Implementation of ConsumerStage::Writer.
  void WritePacket(int64_t start_frame, int64_t length, void* payload) final {
    packets_.push_back({
        .is_silence = false,
        .start_frame = start_frame,
        .length = length,
        .payload = payload,
    });
    if (on_write_packet_) {
      on_write_packet_(start_frame, length, payload);
    }
  }

  void WriteSilence(int64_t start_frame, int64_t length) final {
    packets_.push_back({
        .is_silence = true,
        .start_frame = start_frame,
        .length = length,
        .payload = nullptr,
    });
    if (on_write_silence_) {
      on_write_silence_(start_frame, length);
    }
  }

  // Optional callbacks, to get notifications in addition to the saved packets.
  void SetOnWritePacket(std::function<void(int64_t, int64_t, void*)> fn) {
    on_write_packet_ = std::move(fn);
  }
  void SetOnWriteSilence(std::function<void(int64_t, int64_t)> fn) {
    on_write_silence_ = std::move(fn);
  }

 private:
  std::vector<Packet> packets_;
  std::function<void(int64_t, int64_t, void*)> on_write_packet_;
  std::function<void(int64_t, int64_t)> on_write_silence_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_CONSUMER_STAGE_WRITER_H_
