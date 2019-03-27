// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/source_impl.h"

#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <sstream>
#include "lib/fidl/cpp/optional.h"
#include "lib/fidl/cpp/type_converter.h"
#include "lib/fxl/logging.h"
#include "src/media/playback/mediaplayer_tmp/core/demux_source_segment.h"
#include "src/media/playback/mediaplayer_tmp/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer_tmp/fidl/simple_stream_sink_impl.h"
#include "src/media/playback/mediaplayer_tmp/util/safe_clone.h"

namespace media_player {

SourceImpl::SourceImpl(Graph* graph, fit::closure connection_failure_callback)
    : graph_(graph),
      connection_failure_callback_(std::move(connection_failure_callback)),
      dispatcher_(async_get_default_dispatcher()) {
  FXL_DCHECK(graph_);
  FXL_DCHECK(dispatcher_);
}

SourceImpl::~SourceImpl() {}

void SourceImpl::CompleteConstruction(SourceSegment* source_segment) {
  FXL_DCHECK(source_segment);

  source_segment_ = source_segment;

  source_segment_->Provision(
      graph_, dispatcher_,
      [this]() {
        // This callback notifies this |SourceImpl| of
        // changes to source_segment_'s problem() and/or
        // metadata() values.
        SendStatusUpdates();
      },
      [this](size_t index, const SourceSegment::Stream* stream, bool more) {
        if (stream) {
          OnStreamUpdated(index, *stream);
        } else {
          OnStreamRemoved(index);
        }

        if (!more) {
          SendStatusUpdates();
        }
      });
}

void SourceImpl::OnStreamUpdated(size_t index,
                                 const SourceSegment::Stream& update_stream) {
  if (streams_.size() < index + 1) {
    streams_.resize(index + 1);
  }

  Stream& stream = streams_[index];

  stream.stream_type_ = update_stream.type().Clone();
  stream.output_ = update_stream.output();
}

void SourceImpl::OnStreamRemoved(size_t index) {
  if (streams_.size() < index + 1) {
    return;
  }

  Stream& stream = streams_[index];

  stream.stream_type_ = nullptr;
  stream.output_ = nullptr;

  // Remove unused entries at the back of streams_.
  while (!streams_.empty() && !streams_.back().stream_type_) {
    streams_.pop_back();
  }
}

void SourceImpl::SendStatusUpdates() { UpdateStatus(); }

void SourceImpl::Clear() {
  source_segment_ = nullptr;
  streams_.clear();
  status_ = fuchsia::mediaplayer::SourceStatus();
}

void SourceImpl::Remove() {
  if (connection_failure_callback_) {
    connection_failure_callback_();
  }
}

void SourceImpl::UpdateStatus() {
  status_.has_audio = false;
  status_.has_video = false;

  for (auto& stream : streams_) {
    if (stream.stream_type_) {
      switch (stream.stream_type_->medium()) {
        case StreamType::Medium::kAudio:
          status_.has_audio = true;
          break;
        case StreamType::Medium::kVideo:
          status_.has_video = true;
          break;
        case StreamType::Medium::kText:
        case StreamType::Medium::kSubpicture:
          FXL_NOTIMPLEMENTED();
          break;
      }
    }
  }

  status_.duration_ns = source_segment_->duration_ns();
  status_.can_pause = source_segment_->can_pause();
  status_.can_seek = source_segment_->can_seek();

  auto metadata = source_segment_->metadata();
  status_.metadata =
      metadata
          ? fidl::MakeOptional(fidl::To<fuchsia::media::Metadata>(*metadata))
          : nullptr;

  status_.problem = SafeClone(source_segment_->problem());
}

////////////////////////////////////////////////////////////////////////////////
// DemuxSourceImpl implementation.

// static
std::unique_ptr<DemuxSourceImpl> DemuxSourceImpl::Create(
    std::shared_ptr<Demux> demux, Graph* graph,
    fidl::InterfaceRequest<fuchsia::mediaplayer::Source> request,
    fit::closure connection_failure_callback) {
  FXL_DCHECK(demux);
  FXL_DCHECK(graph);
  return std::make_unique<DemuxSourceImpl>(
      demux, graph, std::move(request), std::move(connection_failure_callback));
}

DemuxSourceImpl::DemuxSourceImpl(
    std::shared_ptr<Demux> demux, Graph* graph,
    fidl::InterfaceRequest<fuchsia::mediaplayer::Source> request,
    fit::closure connection_failure_callback)
    : SourceImpl(graph, std::move(connection_failure_callback)),
      demux_(demux),
      binding_(this),
      demux_source_segment_(DemuxSourceSegment::Create(demux_)) {
  FXL_DCHECK(demux_);

  if (request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) { Remove(); });
  }

  SourceImpl::CompleteConstruction(demux_source_segment_.get());
}

DemuxSourceImpl::~DemuxSourceImpl() {}

std::unique_ptr<SourceSegment> DemuxSourceImpl::TakeSourceSegment() {
  FXL_DCHECK(demux_source_segment_);
  Clear();
  return std::move(demux_source_segment_);
}

void DemuxSourceImpl::SendStatusUpdates() {
  SourceImpl::SendStatusUpdates();

  if (binding_.is_bound()) {
    binding_.events().OnStatusChanged(fidl::Clone(status()));
  }
}

////////////////////////////////////////////////////////////////////////////////
// StreamSourceImpl implementation.

// static
std::unique_ptr<StreamSourceImpl> StreamSourceImpl::Create(
    int64_t duration_ns, bool can_pause, bool can_seek,
    std::unique_ptr<fuchsia::media::Metadata> metadata, Graph* graph,
    fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource> request,
    fit::closure connection_failure_callback) {
  FXL_DCHECK(graph);
  FXL_DCHECK(request);
  return std::make_unique<StreamSourceImpl>(
      duration_ns, can_pause, can_seek, std::move(metadata), graph,
      std::move(request), std::move(connection_failure_callback));
}

StreamSourceImpl::StreamSourceImpl(
    int64_t duration_ns, bool can_pause, bool can_seek,
    std::unique_ptr<fuchsia::media::Metadata> metadata, Graph* graph,
    fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource> request,
    fit::closure connection_failure_callback)
    : SourceImpl(graph, std::move(connection_failure_callback)),
      stream_source_segment_(StreamSourceSegment::Create(
          duration_ns, can_pause, can_seek,
          fidl::To<std::unique_ptr<media_player::Metadata>>(metadata))) {
  SourceImpl::CompleteConstruction(stream_source_segment_.get());

  // We keep a raw pointer around and use that, because we still want to be
  // able to add streams to the source segment after |TakeSourceSegment| is
  // called. This is a bit weird but safe as long as this |StreamSourceImpl|
  // is destroyed before the source segment is destroyed. |PlayerImpl| is
  // careful about that.
  stream_source_segment_raw_ptr_ = stream_source_segment_.get();

  AddBindingInternal(std::move(request));

  bindings_.set_empty_set_handler([this]() { Remove(); });
}

StreamSourceImpl::~StreamSourceImpl() {}

std::unique_ptr<SourceSegment> StreamSourceImpl::TakeSourceSegment() {
  FXL_DCHECK(stream_source_segment_);
  // We don't call |Clear|, because we want this |StreamSourceImpl| to continue
  // to function event without |stream_source_segment_| set.
  return std::move(stream_source_segment_);
}

void StreamSourceImpl::SendStatusUpdates() {
  SourceImpl::SendStatusUpdates();

  for (auto& binding : bindings_.bindings()) {
    binding->events().OnStatusChanged(fidl::Clone(status()));
  }
}

void StreamSourceImpl::AddStream(
    fuchsia::media::StreamType type, uint32_t tick_per_second_numerator,
    uint32_t tick_per_second_denominator,
    ::fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink>
        simple_stream_sink_request) {
  FXL_DCHECK(simple_stream_sink_request);
  FXL_DCHECK(stream_source_segment_raw_ptr_);

  auto output_stream_type =
      fidl::To<std::unique_ptr<media_player::StreamType>>(type);
  FXL_DCHECK(output_stream_type);

  stream_source_segment_raw_ptr_->AddStream(
      SimpleStreamSinkImpl::Create(
          *output_stream_type,
          media::TimelineRate(tick_per_second_numerator,
                              tick_per_second_denominator),
          std::move(simple_stream_sink_request)),
      *output_stream_type);
}

void StreamSourceImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource>
        stream_source_request) {
  FXL_DCHECK(stream_source_request);
  AddBindingInternal(std::move(stream_source_request));
}

void StreamSourceImpl::AddBindingInternal(
    fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource>
        stream_source_request) {
  FXL_DCHECK(stream_source_request);

  bindings_.AddBinding(this, std::move(stream_source_request));

  // Fire |OnStatusChanged| event for the new client.
  bindings_.bindings().back()->events().OnStatusChanged(fidl::Clone(status()));
}

}  // namespace media_player
