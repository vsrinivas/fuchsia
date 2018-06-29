// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_RENDERER_SINK_SEGMENT_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_RENDERER_SINK_SEGMENT_H_

#include "garnet/bin/media/media_player/decode/decoder.h"
#include "garnet/bin/media/media_player/player/sink_segment.h"
#include "garnet/bin/media/media_player/render/renderer.h"

namespace media_player {

// A graph segment that delivers an elementary stream to a renderer.
class RendererSinkSegment : public SinkSegment {
 public:
  static std::unique_ptr<RendererSinkSegment> Create(
      std::shared_ptr<Renderer> renderer, DecoderFactory* decoder_factory);

  RendererSinkSegment(std::shared_ptr<Renderer> renderer,
                      DecoderFactory* decoder_factory);

  ~RendererSinkSegment() override;

  // SinkSegment overrides.
  void DidProvision() override;

  void WillDeprovision() override;

  void Connect(const StreamType& type, OutputRef output,
               ConnectCallback callback) override;

  void Disconnect() override;

  bool connected() const override { return !!connected_output_; }

  void Prepare() override;

  void Unprepare() override;

  void Prime(fit::closure callback) override;

  void SetTimelineFunction(media::TimelineFunction timeline_function,
                           fit::closure callback) override;

  void SetProgramRange(uint64_t program, int64_t min_pts,
                       int64_t max_pts) override;

  bool end_of_stream() const override { return renderer_->end_of_stream(); }

 private:
  std::shared_ptr<Renderer> renderer_;
  DecoderFactory* decoder_factory_;
  NodeRef renderer_node_;
  OutputRef connected_output_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_RENDERER_SINK_SEGMENT_H_
