// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/core/stream_source_segment.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer_tmp/util/callback_joiner.h"
#include "src/media/playback/mediaplayer_tmp/util/safe_clone.h"

namespace media_player {

// static
std::unique_ptr<StreamSourceSegment> StreamSourceSegment::Create(
    int64_t duration_ns, bool can_pause, bool can_seek,
    std::unique_ptr<media_player::Metadata> metadata) {
  return std::make_unique<StreamSourceSegment>(duration_ns, can_pause, can_seek,
                                               std::move(metadata));
}

StreamSourceSegment::StreamSourceSegment(
    int64_t duration_ns, bool can_pause, bool can_seek,
    std::unique_ptr<media_player::Metadata> metadata)
    : SourceSegment(false),
      duration_ns_(duration_ns),
      can_pause_(can_pause),
      can_seek_(can_seek),
      metadata_(std::move(metadata)) {}

StreamSourceSegment::~StreamSourceSegment() {}

void StreamSourceSegment::AddStream(std::shared_ptr<Node> node,
                                    const StreamType& output_stream_type) {
  FXL_DCHECK(node);

  size_t index = nodes_.size();

  nodes_.push_back(graph().Add(node));

  OnStreamUpdated(index, output_stream_type, nodes_.back().output(),
                  false);  // more
}

void StreamSourceSegment::DidProvision() {
  async::PostTask(dispatcher(), [this, weak_this = GetWeakThis()]() {
    if (!weak_this) {
      return;
    }

    if (provisioned()) {
      NotifyUpdate();
    }
  });
}

void StreamSourceSegment::WillDeprovision() {
  for (auto node_ref : nodes_) {
    graph().RemoveNode(node_ref);
  }
}

void StreamSourceSegment::Flush(bool hold_frame, fit::closure callback) {
  auto callback_joiner = CallbackJoiner::Create();

  for (auto node_ref : nodes_) {
    graph().FlushOutput(node_ref.output(), hold_frame,
                        callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined(std::move(callback));
}

void StreamSourceSegment::Seek(int64_t position, fit::closure callback) {
  FXL_DCHECK(can_seek_);
  // TODO(dalesat): Implement.
  FXL_NOTIMPLEMENTED();
}

}  // namespace media_player
