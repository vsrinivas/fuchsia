// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_CORE_RENDERER_SINK_SEGMENT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_CORE_RENDERER_SINK_SEGMENT_H_

#include "src/media/playback/mediaplayer_tmp/core/sink_segment.h"
#include "src/media/playback/mediaplayer_tmp/decode/decoder.h"
#include "src/media/playback/mediaplayer_tmp/render/renderer.h"

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

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_CORE_RENDERER_SINK_SEGMENT_H_
