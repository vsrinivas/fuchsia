// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/player/source_segment.h"

#include <lib/async/dispatcher.h>

#include "lib/fxl/logging.h"

namespace media_player {

SourceSegment::SourceSegment() {}

SourceSegment::~SourceSegment() {}

void SourceSegment::Provision(Graph* graph, async_dispatcher_t* dispatcher,
                              fit::closure updateCallback,
                              StreamUpdateCallback stream_update_callback) {
  stream_update_callback_ = std::move(stream_update_callback);
  Segment::Provision(graph, dispatcher, std::move(updateCallback));
}

void SourceSegment::Deprovision() {
  Segment::Deprovision();
  stream_update_callback_ = nullptr;
}

void SourceSegment::OnStreamUpdated(size_t index, const StreamType& type,
                                    OutputRef output, bool more) {
  FXL_DCHECK(stream_update_callback_)
      << "OnStreamUpdated() called on unprovisioned segment.";

  stream_update_callback_(index, &type, output, more);
}

void SourceSegment::OnStreamRemoved(size_t index, bool more) {
  FXL_DCHECK(stream_update_callback_)
      << "OnStreamRemoved() called on unprovisioned segment.";

  stream_update_callback_(index, nullptr, OutputRef(), more);
}

}  // namespace media_player
