// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_CORE_SOURCE_SEGMENT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_CORE_SOURCE_SEGMENT_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <vector>
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "src/media/playback/mediaplayer_tmp/core/segment.h"
#include "src/media/playback/mediaplayer_tmp/graph/graph.h"

namespace media_player {

// Abstract base class for a graph segment that produces elementary streams.
//
// Note that the update callback supplied in Segment::Provision is used to
// notify of changes to the values returned by duration_ns(), can_pause(),
// can_seek() and metadata().
// TODO(dalesat): Consider moving metadata out of this definition. Not all
// sources will provide metadata, and there's no reason why Player should be
// concerned with metadata.
class SourceSegment : public Segment {
 public:
  // Describes a stream.
  struct Stream {
   public:
    // Indicates whether this stream is valid. An invalid stream is a
    // placeholder for a removed stream.
    bool valid() const {
      FXL_DCHECK((stream_type_ != nullptr) == static_cast<bool>(output_));
      return stream_type_ != nullptr;
    }

    // Gets the type of the stream. This method must not be called on an invalid
    // |Stream|.
    const StreamType& type() const {
      FXL_DCHECK(stream_type_);
      return *stream_type_;
    }

    // The output that produces the stream. This method must not be called on
    // an invalid |Stream|.
    const OutputRef output() const {
      FXL_DCHECK(output_);
      return output_;
    }

   private:
    std::unique_ptr<StreamType> stream_type_;
    OutputRef output_;

    friend class SourceSegment;
  };

  // Callback type used to inform the owner of stream changes. Stream adds
  // and updated are indicated by non-null |stream| value. Stream removes are
  // indicated by null |stream| value. |more| is true during initial stream
  // enumeration when the segment knows there are more streams to report.
  using StreamUpdateCallback =
      fit::function<void(size_t index, const Stream* stream, bool more)>;

  // Constructs a |SourceSegment|. |stream_add_imminent| should be true if the
  // subclass will immediately enumerate streams after |Provision| is called.
  // It would be false if the subclass can't control when streams are
  // enumerated.
  SourceSegment(bool stream_add_imminent);

  ~SourceSegment() override;

  // Provides the graph, async and callbacks for this source segment.
  // |updateCallback| and |stream_update_callback| are both optional. If the
  // segment can decide when streams are enumerated, it does so immediately
  // after this method is called.
  void Provision(Graph* graph, async_dispatcher_t* dispatcher,
                 fit::closure updateCallback,
                 StreamUpdateCallback stream_update_callback);

  // Revokes the graph, task runner and callbacks provided in a previous call
  // to |Provision|.
  void Deprovision() override;

  // Sets the stream update callback. |stream_update_callback| may be null.
  void SetStreamUpdateCallback(StreamUpdateCallback stream_update_callback);

  const std::vector<Stream>& streams() { return streams_; }

  // Indicates whether the addition of one or more streams is imminent. A false
  // value is no guarantee that more streams won't be added.
  bool stream_add_imminent() const { return stream_add_imminent_; }

  // Returns the duration of the content in nanoseconds or 0 if the duration is
  // currently unknown.
  virtual int64_t duration_ns() const = 0;

  // Indicates whether this segment can pause.
  virtual bool can_pause() const = 0;

  // Indicates whether this segment can seek.
  virtual bool can_seek() const = 0;

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
  // Gets a weak pointer to this |SourceSegment|.
  fxl::WeakPtr<SourceSegment> GetWeakThis() {
    return weak_factory_.GetWeakPtr();
  }

  // Called by subclasses when a stream is updated.
  void OnStreamUpdated(size_t index, const StreamType& type, OutputRef output,
                       bool more);

  // Called by subclasses when a stream is removed.
  void OnStreamRemoved(size_t index, bool more);

 private:
  fxl::WeakPtrFactory<SourceSegment> weak_factory_;
  bool stream_add_imminent_ = true;
  StreamUpdateCallback stream_update_callback_;
  // TODO(dalesat): Do we really need to maintain this or can we just have an
  // abstract GetStreams()?
  std::vector<Stream> streams_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_CORE_SOURCE_SEGMENT_H_
