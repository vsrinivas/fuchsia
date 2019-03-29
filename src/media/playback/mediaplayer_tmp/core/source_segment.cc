// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/core/source_segment.h"

#include <lib/async/dispatcher.h>
#include "src/lib/fxl/logging.h"

namespace media_player {

SourceSegment::SourceSegment(bool stream_add_imminent)
    : weak_factory_(this), stream_add_imminent_(stream_add_imminent) {}

SourceSegment::~SourceSegment() {}

void SourceSegment::Provision(Graph* graph, async_dispatcher_t* dispatcher,
                              fit::closure updateCallback,
                              StreamUpdateCallback stream_update_callback) {
  FXL_DCHECK(graph);
  FXL_DCHECK(dispatcher);

  stream_update_callback_ = std::move(stream_update_callback);
  Segment::Provision(graph, dispatcher, std::move(updateCallback));
}

void SourceSegment::Deprovision() {
  Segment::Deprovision();
  stream_update_callback_ = nullptr;
}

void SourceSegment::SetStreamUpdateCallback(
    StreamUpdateCallback stream_update_callback) {
  stream_update_callback_ = std::move(stream_update_callback);
}

void SourceSegment::OnStreamUpdated(size_t index, const StreamType& type,
                                    OutputRef output, bool more) {
  FXL_DCHECK(output);

  if (streams_.size() <= index) {
    streams_.resize(index + 1);
  }

  Stream& stream = streams_[index];
  stream.stream_type_ = type.Clone();
  stream.output_ = output;

  stream_add_imminent_ = more;

  if (stream_update_callback_) {
    stream_update_callback_(index, &stream, more);
  }
}

void SourceSegment::OnStreamRemoved(size_t index, bool more) {
  FXL_DCHECK(index < streams_.size());

  Stream& stream = streams_[index];
  stream.stream_type_ = nullptr;
  stream.output_ = nullptr;

  stream_add_imminent_ = more;

  if (stream_update_callback_) {
    stream_update_callback_(index, nullptr, more);
  }

  // Trim invalid elements of the end of |streams_|.
  while (!streams_.empty() && !streams_.back().valid()) {
    streams_.pop_back();
  }
}

}  // namespace media_player
