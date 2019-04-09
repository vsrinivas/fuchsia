// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/core/demux_source_segment.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"

namespace media_player {

// static
std::unique_ptr<DemuxSourceSegment> DemuxSourceSegment::Create(
    std::shared_ptr<Demux> demux) {
  return std::make_unique<DemuxSourceSegment>(demux);
}

DemuxSourceSegment::DemuxSourceSegment(std::shared_ptr<Demux> demux)
    : SourceSegment(true), demux_(demux) {
  FXL_DCHECK(demux_);

  demux_->SetStatusCallback([this](int64_t duration_ns, bool can_seek,
                                   const Metadata& metadata,
                                   const std::string& problem_type,
                                   const std::string& problem_details) {
    duration_ns_ = duration_ns;

    can_seek_ = can_seek;

    if (metadata.empty()) {
      metadata_ = nullptr;
    } else {
      metadata_ = std::make_unique<Metadata>(metadata);
    }

    NotifyUpdate();

    if (problem_type.empty()) {
      ReportNoProblem();
    } else {
      ReportProblem(problem_type, problem_details);
    }
  });

  demux_->WhenInitialized(
      [this](zx_status_t status) { demux_initialized_.Occur(); });
}

DemuxSourceSegment::~DemuxSourceSegment() {}

void DemuxSourceSegment::DidProvision() {
  demux_initialized_.When([this]() {
    async::PostTask(dispatcher(), [this]() {
      if (provisioned()) {
        BuildGraph();
      }
    });
  });
}

void DemuxSourceSegment::WillDeprovision() {
  if (demux_node_) {
    graph().RemoveNode(demux_node_);
    demux_node_ = nullptr;
  }

  if (demux_) {
    demux_->SetStatusCallback(nullptr);
    demux_ = nullptr;
  }
}

void DemuxSourceSegment::BuildGraph() {
  demux_node_ = graph().Add(demux_);

  const auto& streams = demux_->streams();
  for (size_t index = 0; index < streams.size(); ++index) {
    auto& stream = streams[index];
    OnStreamUpdated(stream->index(), *stream->stream_type(),
                    demux_node_.output(stream->index()),
                    index != streams.size() - 1);
  }
}

void DemuxSourceSegment::Flush(bool hold_frame, fit::closure callback) {
  FXL_DCHECK(demux_initialized_.occurred());
  graph().FlushAllOutputs(demux_node_, hold_frame, std::move(callback));
}

void DemuxSourceSegment::Seek(int64_t position, fit::closure callback) {
  FXL_DCHECK(demux_initialized_.occurred());
  FXL_DCHECK(can_seek_);
  demux_->Seek(position, std::move(callback));
}

}  // namespace media_player
