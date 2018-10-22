// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/source_impl.h"

#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <sstream>
#include "garnet/bin/mediaplayer/core/demux_source_segment.h"
#include "garnet/bin/mediaplayer/fidl/fidl_type_conversions.h"
#include "garnet/bin/mediaplayer/util/safe_clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/type_converter.h"

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
      metadata ? fidl::MakeOptional(
                     fxl::To<fuchsia::mediaplayer::Metadata>(*metadata))
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
    binding_.set_error_handler([this]() { Remove(); });
  }

  SourceImpl::CompleteConstruction(demux_source_segment_.get());
}

DemuxSourceImpl::~DemuxSourceImpl() {}

std::unique_ptr<SourceSegment> DemuxSourceImpl::TakeSourceSegment() {
  Clear();
  return std::move(demux_source_segment_);
}

void DemuxSourceImpl::SendStatusUpdates() {
  SourceImpl::SendStatusUpdates();

  if (binding_.is_bound()) {
    binding_.events().OnStatusChanged(fidl::Clone(status()));
  }
}

}  // namespace media_player
