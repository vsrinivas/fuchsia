// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/player/renderer_sink_segment.h"

#include "garnet/bin/mediaplayer/framework/types/stream_type.h"
#include "garnet/bin/mediaplayer/player/conversion_pipeline_builder.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace media_player {

// static
std::unique_ptr<RendererSinkSegment> RendererSinkSegment::Create(
    std::shared_ptr<Renderer> renderer, DecoderFactory* decoder_factory) {
  return std::make_unique<RendererSinkSegment>(renderer, decoder_factory);
}

RendererSinkSegment::RendererSinkSegment(std::shared_ptr<Renderer> renderer,
                                         DecoderFactory* decoder_factory)
    : renderer_(renderer), decoder_factory_(decoder_factory) {
  FXL_DCHECK(renderer_);
  FXL_DCHECK(decoder_factory_);
}

RendererSinkSegment::~RendererSinkSegment() {}

void RendererSinkSegment::DidProvision() {
  renderer_node_ = graph().Add(renderer_);

  renderer_->Provision(dispatcher(), [this]() { NotifyUpdate(); });
}

void RendererSinkSegment::WillDeprovision() {
  renderer_->Deprovision();

  if (renderer_node_) {
    graph().RemoveNode(renderer_node_);
    renderer_node_ = nullptr;
  }
}

void RendererSinkSegment::Connect(const StreamType& type, OutputRef output,
                                  ConnectCallback callback) {
  FXL_DCHECK(provisioned());
  FXL_DCHECK(renderer_);
  FXL_DCHECK(renderer_node_);

  connected_output_ = output;

  BuildConversionPipeline(
      type, renderer_->GetSupportedStreamTypes(), &graph(), decoder_factory_,
      output,
      [this, callback = std::move(callback),
       is_audio = type.medium() == StreamType::Medium::kAudio](
          OutputRef output, std::unique_ptr<StreamType> stream_type) {
        if (!stream_type) {
          ReportProblem(
              is_audio
                  ? fuchsia::mediaplayer::kProblemAudioEncodingNotSupported
                  : fuchsia::mediaplayer::kProblemVideoEncodingNotSupported,
              "");
          connected_output_ = nullptr;
          callback(Result::kUnsupportedOperation);
          return;
        }

        graph().ConnectOutputToNode(output, renderer_node_);
        renderer_->SetStreamType(*stream_type);
        callback(Result::kOk);
      });
}

void RendererSinkSegment::Disconnect() {
  FXL_DCHECK(provisioned());
  FXL_DCHECK(renderer_node_);
  FXL_DCHECK(connected_output_);

  // TODO(dalesat): Consider keeping the conversions until we know they won't
  // work for the next connection.

  graph().DisconnectOutput(connected_output_);
  graph().RemoveNodesConnectedToInput(renderer_node_.input());

  connected_output_ = nullptr;
}

void RendererSinkSegment::Prepare() {
  FXL_DCHECK(provisioned());
  FXL_DCHECK(renderer_node_);
  FXL_DCHECK(connected_output_);

  graph().PrepareInput(renderer_node_.input());
}

void RendererSinkSegment::Unprepare() {
  FXL_DCHECK(provisioned());
  FXL_DCHECK(renderer_node_);
  FXL_DCHECK(connected_output_);

  if (renderer_node_.input().prepared()) {
    graph().UnprepareInput(renderer_node_.input());
  }
}

void RendererSinkSegment::Prime(fit::closure callback) {
  FXL_DCHECK(renderer_);
  renderer_->Prime(std::move(callback));
}

void RendererSinkSegment::SetTimelineFunction(
    media::TimelineFunction timeline_function, fit::closure callback) {
  FXL_DCHECK(renderer_);
  renderer_->SetTimelineFunction(timeline_function, std::move(callback));
}

void RendererSinkSegment::SetProgramRange(uint64_t program, int64_t min_pts,
                                          int64_t max_pts) {
  FXL_DCHECK(renderer_);
  renderer_->SetProgramRange(program, min_pts, max_pts);
}

}  // namespace media_player
