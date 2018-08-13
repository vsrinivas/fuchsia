// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_AUDIO_RENDERER_H_
#define GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_AUDIO_RENDERER_H_

#include "garnet/bin/mediaplayer/render/renderer.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace test {

class FakeAudioRenderer : public Renderer {
 public:
  static std::shared_ptr<FakeAudioRenderer> Create();

  FakeAudioRenderer();

  ~FakeAudioRenderer() override {}

  const char* label() const override { return "FakeAudioRenderer"; }

  // Renderer implementation.
  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override {
    FXL_DCHECK(input_index == 0);
    FXL_DCHECK(callback);
    callback();
  }

  void PutInputPacket(PacketPtr packet, size_t input_index) override {
    FXL_DCHECK(packet);
    FXL_DCHECK(input_index == 0);
    // Throw away the packet and request a new one.
    // TODO(dalesat): Simulate real renderer timing and stop requestion on eos.
    stage()->RequestInputPacket();
  }

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes()
      override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override {
    stream_type_ = stream_type.Clone();
  }

  void Prime(fit::closure callback) override { callback(); }

  void SetTimelineFunction(media::TimelineFunction timeline_function,
                           fit::closure callback) override {
    callback();
  }

  void SetProgramRange(uint64_t program, int64_t min_pts,
                       int64_t max_pts) override {}

 private:
  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  std::unique_ptr<StreamType> stream_type_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_AUDIO_RENDERER_H_
