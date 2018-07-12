// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_SOURCE_SEGMENT_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_SOURCE_SEGMENT_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/media_player/framework/graph.h"
#include "garnet/bin/media/media_player/player/segment.h"

namespace media_player {

// A graph segment that produces elementary streams.
//
// Note that the update callback supplied in Segment::Provision is used to
// notify of changes to the value returned by metadata().
// TODO(dalesat): Consider moving metadata out of this definition. Not all
// sources will provide metadata, and there's no reason why Player should be
// concerned with metadata.
class SourceSegment : public Segment {
 public:
  using StreamUpdateCallback = fit::function<void(
      size_t index, const StreamType* type, OutputRef output, bool more)>;

  SourceSegment();

  ~SourceSegment() override;

  // Provides the graph, async and callbacks for this source segment.
  // The player expects stream updates shortly after this method is called,
  // the last of which should have a |more| value of false.
  void Provision(Graph* graph, async_dispatcher_t* dispatcher, fit::closure updateCallback,
                 StreamUpdateCallback stream_update_callback);

  // Revokes the graph, task runner and callbacks provided in a previous call to
  // |Provision|.
  void Deprovision() override;

  // Returns the metadata for the current content or nullptr if no metadata
  // has been obtained.
  virtual const Metadata* metadata() const = 0;

  // Flushes the source.
  virtual void Flush(bool hold_frame, fit::closure callback) = 0;

  // Seeks to the specified position.
  virtual void Seek(int64_t position, fit::closure callback) = 0;

  // Test only.
  // Returns a reference to the source node.
  virtual NodeRef source_node() const { return NodeRef(); }

 protected:
  // Called by subclasses when a stream is updated.
  void OnStreamUpdated(size_t index, const StreamType& type, OutputRef output,
                       bool more);

  // Called by subclasses when a stream is removed.
  void OnStreamRemoved(size_t index, bool more);

  StreamUpdateCallback stream_update_callback_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_SOURCE_SEGMENT_H_
