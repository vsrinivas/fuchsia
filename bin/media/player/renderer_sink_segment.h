// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/player/sink_segment.h"
#include "garnet/bin/media/render/renderer.h"

namespace media {

// A graph segment that delivers an elementary stream to a renderer.
class RendererSinkSegment : public SinkSegment {
 public:
  static std::unique_ptr<RendererSinkSegment> Create(
      std::shared_ptr<Renderer> renderer);

  RendererSinkSegment(std::shared_ptr<Renderer> renderer);

  ~RendererSinkSegment() override;

  // SinkSegment overrides.
  void DidProvision() override;

  void WillDeprovision() override;

  void Connect(const StreamType& type,
               OutputRef output,
               fxl::Closure callback) override;

  void Disconnect() override;

  bool connected() const override { return !!connected_output_; }

  void Prepare() override;

  void Unprepare() override;

  void Prime(fxl::Closure callback) override;

  void SetTimelineFunction(TimelineFunction timeline_function,
                           fxl::Closure callback) override;

  void SetProgramRange(uint64_t program,
                       int64_t min_pts,
                       int64_t max_pts) override;

  bool end_of_stream() const override { return renderer_->end_of_stream(); }

 private:
  std::shared_ptr<Renderer> renderer_;
  NodeRef renderer_node_;
  OutputRef connected_output_;
};

}  // namespace media
