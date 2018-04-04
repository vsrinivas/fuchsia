// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/player/source_segment.h"

#include "lib/fxl/logging.h"

namespace media {

SourceSegment::SourceSegment() {}

SourceSegment::~SourceSegment() {}

void SourceSegment::Provision(Graph* graph,
                              fxl::RefPtr<fxl::TaskRunner> task_runner,
                              fxl::Closure updateCallback,
                              StreamUpdateCallback stream_update_callback) {
  stream_update_callback_ = stream_update_callback;
  Segment::Provision(graph, task_runner, updateCallback);
}

void SourceSegment::Deprovision() {
  Segment::Deprovision();
  stream_update_callback_ = nullptr;
}

void SourceSegment::OnStreamUpdated(size_t index,
                                   const StreamType& type,
                                   OutputRef output,
                                   bool more) {
  FXL_DCHECK(stream_update_callback_)
      << "OnStreamUpdated() called on unprovisioned segment.";

  stream_update_callback_(index, &type, output, more);
}

void SourceSegment::OnStreamRemoved(size_t index, bool more) {
  FXL_DCHECK(stream_update_callback_)
      << "OnStreamRemoved() called on unprovisioned segment.";

  stream_update_callback_(index, nullptr, OutputRef(), more);
}

}  // namespace media
