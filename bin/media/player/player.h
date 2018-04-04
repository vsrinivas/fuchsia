// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include <vector>

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/framework/graph.h"
#include "garnet/bin/media/framework/metadata.h"
#include "garnet/bin/media/player/sink_segment.h"
#include "garnet/bin/media/player/source_segment.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

// A graph that delivers content one origin to many destinations.
class Player {
 public:
  Player(fxl::RefPtr<fxl::TaskRunner> task_runner);

  ~Player();

  // Sets the callback to be called when the status of the player is updated.
  // This callback notifies of changes to end_of_stream(), metadata() and/or
  // problem().
  void SetUpdateCallback(fxl::Closure update_callback) {
    update_callback_ = update_callback;
  }

  // Sets the current source segment. |source_segment| may be null, indicating
  // there is no source segment. The callback is called when the initial set
  // of streams supplied by the segment have been connected and prepared to the
  // extent possible. |callback| may be null.
  void SetSourceSegment(std::unique_ptr<SourceSegment> source_segment,
                        fxl::Closure callback);

  // Sets the current sink segment for the specified medium. |sink_segment| may
  // be null, indicating there is no sink segment for the specified medium.
  void SetSinkSegment(std::unique_ptr<SinkSegment> sink_segment,
                      StreamType::Medium medium);

  // Indicates whether the player has a source segment.
  bool has_source_segment() const { return !!source_segment_; }

  // Indicates whether the player has a sink segment for the specified medium.
  bool has_sink_segment(StreamType::Medium medium) const {
    if (GetParkedSinkSegment(medium)) {
      return true;
    }

    const Stream* stream = GetStream(medium);
    return stream && stream->sink_segment_;
  }

  // Indicates whether the currently-loaded content has a stream with the
  // specified medium.
  bool content_has_medium(StreamType::Medium medium) const {
    return !!GetStream(medium);
  }

  // Indicates whether the indicated medium is connected to a sink segment. This
  // will be false if no sink segment for the specified medium has been supplied
  // or the provided sink segment could not handle the stream type.
  bool medium_connected(StreamType::Medium medium) const {
    const Stream* stream = GetStream(medium);
    return stream && stream->sink_segment_ &&
           stream->sink_segment_->connected();
  }

  // Prepares the graph for playback by satisfying initial renderer demand.
  void Prime(fxl::Closure callback);

  // Flushes packets from the graph.
  void Flush(bool hold_frame);

  // Sets the timeline function.
  void SetTimelineFunction(TimelineFunction timeline_function,
                           fxl::Closure callback);

  const TimelineFunction& timeline_function() { return timeline_function_; }

  // Sets a program range for the renderers.
  void SetProgramRange(uint64_t program, int64_t min_pts, int64_t max_pts);

  // Seeks to the specified position.
  void Seek(int64_t position, fxl::Closure callback);

  // Indicates whether the player has reached end of stream.
  bool end_of_stream() const;

  // Returns the metadata for the current content or nullptr if no metadata
  // has been obtained.
  // TODO(dalesat): Remove metadata concerns from the player and source segment.
  const Metadata* metadata() const;

  // Returns the current problem preventing intended operation or nullptr if
  // there is no such problem.
  const Problem* problem() const;

  // Test only.
  // Returns a pointer to the graph.
  const Graph* graph() const { return &graph_; }

  // Test only.
  // Returns a reference to the source node.
  NodeRef source_node() const {
    return source_segment_ ? source_segment_->source_node() : NodeRef();
  }

 private:
  static constexpr int64_t kMinimumLeadTime = Timeline::ns_from_ms(30);

  struct Stream {
    std::unique_ptr<SinkSegment> sink_segment_;
    std::unique_ptr<StreamType> stream_type_;
    OutputRef output_;
  };

  // Calls the update callback.
  void NotifyUpdate();

  // Gets the stream for the specified medium. Returns nullptr if there is no
  // stream for that medium.
  const Stream* GetStream(StreamType::Medium medium) const;

  // Gets the stream for the specified medium. Returns nullptr if there is no
  // stream for that medium.
  Stream* GetStream(StreamType::Medium medium);

  // Sets a parked sink segment for the specified medium. Returns nullptr if
  // there is no parked sink segment for that medium.
  SinkSegment* GetParkedSinkSegment(StreamType::Medium medium) const;

  // Called when the source segment signals that a stream has been updated.
  void OnStreamUpdated(size_t index, const StreamType& type, OutputRef output);

  // Called when the source segment signals that a stream has been removed.
  void OnStreamRemoval(size_t index);

  // Called when an action kicked off by a call to |SetSourceSegment| completes.
  // If |set_source_segment_callback_| is set, |set_source_segment_countdown_|
  // is decremented. If it transitions to zero, |set_source_segment_callback_|
  // is called and cleared.
  void MaybeCompleteSetSourceSegment();

  // Takes a sink segment for the specified medium from |parked_sink_segments_|
  // or a stream. Returns null if no sink segment has been registered for the
  // specified medium.
  std::unique_ptr<SinkSegment> TakeSinkSegment(StreamType::Medium medium);

  // Takes the sink segment from a stream.
  std::unique_ptr<SinkSegment> TakeSinkSegment(Stream* stream);

  // Connects and prepares the specified stream.
  void ConnectAndPrepareStream(Stream* stream);

  Graph graph_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fxl::Closure update_callback_;
  fxl::Closure set_source_segment_callback_;
  size_t set_source_segment_countdown_;
  std::unique_ptr<SourceSegment> source_segment_;
  std::vector<Stream> streams_;
  std::unordered_map<StreamType::Medium, std::unique_ptr<SinkSegment>>
      parked_sink_segments_;
  TimelineFunction timeline_function_;
};

}  // namespace media
