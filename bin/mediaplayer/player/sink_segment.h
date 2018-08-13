// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_PLAYER_SINK_SEGMENT_H_
#define GARNET_BIN_MEDIAPLAYER_PLAYER_SINK_SEGMENT_H_

#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/framework/graph.h"
#include "garnet/bin/mediaplayer/framework/result.h"
#include "garnet/bin/mediaplayer/framework/types/stream_type.h"
#include "garnet/bin/mediaplayer/player/segment.h"
#include "lib/media/timeline/timeline_function.h"

namespace media_player {

// A graph segment that delivers an elementary stream to one or more
// destinations.
//
// Note that the update callback supplied in Segment::Provision is used to
// notify of changes to the value returned by end_of_stream().
class SinkSegment : public Segment {
 public:
  using ConnectCallback = fit::function<void(Result)>;

  SinkSegment();

  ~SinkSegment() override;

  // Connects (or reconnects) this sink segment to the specified output and
  // sets the stream type. After the callback is called, success can be
  // determined by calling |connected|.
  virtual void Connect(const StreamType& type, OutputRef output,
                       ConnectCallback callback) = 0;

  // Disconnects this sink segment.
  virtual void Disconnect() = 0;

  // Indicates whether the segment is connected.
  virtual bool connected() const = 0;

  // Prepares this sink segment in the |Graph::Prepare| sense. This involves
  // walking the graph upstream assigning allocators to the various nodes.
  virtual void Prepare() = 0;

  // Unprepares this sink segment in the |Graph::Unprepare| sense. This involves
  // disconnecting the nodes from the allocators they were assigned during
  // |Prepare|.
  virtual void Unprepare() = 0;

  // Prepares the sink segment for playback by satisfying initial renderer
  // demand.
  virtual void Prime(fit::closure callback) = 0;

  // Sets the timeline function.
  virtual void SetTimelineFunction(media::TimelineFunction timeline_function,
                                   fit::closure callback) = 0;

  // Sets a program range for this sink segment.
  virtual void SetProgramRange(uint64_t program, int64_t min_pts,
                               int64_t max_pts) = 0;

  // Indicates whether this sink segment has reached end of stream.
  virtual bool end_of_stream() const = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_PLAYER_SINK_SEGMENT_H_
