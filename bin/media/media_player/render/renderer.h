// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_RENDERER_H_

#include <limits>

#include "garnet/bin/media/media_player/framework/models/async_node.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"
#include "lib/media/timeline/timeline_function.h"

namespace media_player {

// Abstract base class for sinks that render packets.
class Renderer : public AsyncNode {
 public:
  Renderer();

  ~Renderer() override;

  // Provides an dispatcher object and update callback to the renderer. The callback
  // should be called to notify of changes in the value returned by
  // end_of_stream(). Subclasses of Renderer may use this callback to signal
  // additional changes.
  void Provision(async_dispatcher_t* dispatcher, fit::closure update_callback);

  // Revokes the task runner and update callback provided in a previous call to
  // |Provision|.
  void Deprovision();

  // AsyncNode implementation.
  void Dump(std::ostream& os) const override;

  void GetConfiguration(size_t* input_count, size_t* output_count) override;

  // Returns the types of the streams the renderer is able
  // to consume.
  virtual const std::vector<std::unique_ptr<StreamTypeSet>>&
  GetSupportedStreamTypes() = 0;

  // Sets the type of stream the renderer will consume.
  virtual void SetStreamType(const StreamType& stream_type) = 0;

  // Prepares renderer for playback by satisfying initial demand.
  virtual void Prime(fit::closure callback) = 0;

  // Sets the timeline function.
  virtual void SetTimelineFunction(media::TimelineFunction timeline_function,
                                   fit::closure callback);

  // Sets a program range for this renderer.
  virtual void SetProgramRange(uint64_t program, int64_t min_pts,
                               int64_t max_pts);

  // Determines whether end-of-stream has been reached.
  bool end_of_stream() const;

 protected:
  async_dispatcher_t* dispatcher() const {
    FXL_DCHECK(dispatcher_) << "dispatcher() called on unprovisioned renderer.";
    return dispatcher_;
  }

  // Notifies of state updates (calls the update callback).
  void NotifyUpdate();

  // Called when the value returned by |Progressing| transitions from false to
  // true. The default implementation does nothing.
  virtual void OnProgressStarted() {}

  // Determines if presentation time is progressing or a pending change will
  // cause it to progress.
  bool Progressing();

  // Sets the PTS at which end of stream will occur. Passing kUnspecifiedTime
  // indicates that end-of-stream PTS isn't known.
  void SetEndOfStreamPts(int64_t end_of_stream_pts);

  // Checks for timeline transitions or end-of-stream. |reference_time| is the
  // current reference time.
  void UpdateTimeline(int64_t reference_time);

  // Posts a task to check for timeline transitions or end-of-stream at the
  // specified reference time.
  void UpdateTimelineAt(int64_t reference_time);

  // Called when the timeline function changes. The default implementation
  // does nothing.
  virtual void OnTimelineTransition();

  // Gets the current timeline function.
  const media::TimelineFunction& current_timeline_function() const {
    return current_timeline_function_;
  }

  // Indicates whether the end of stream packet has been encountered.
  bool end_of_stream_pending() const {
    return end_of_stream_pts_ != fuchsia::media::kUnspecifiedTime;
  }

  // PTS at which end-of-stream is to occur or |kUnspecifiedTime| if an end-
  // of-stream packet has not yet been encountered.
  int64_t end_of_stream_pts() const { return end_of_stream_pts_; }

  // Returns the minimum PTS for the specified program.
  int64_t min_pts(uint64_t program) {
    FXL_DCHECK(program == 0);
    return program_0_min_pts_;
  }

  // Returns the maximum PTS for the specified program.
  int64_t max_pts(uint64_t program) {
    FXL_DCHECK(program == 0);
    return program_0_max_pts_;
  }

 private:
  // Applies pending_timeline_function_ if it's time to do so based on the
  // given reference time.
  void ApplyPendingChanges(int64_t reference_time);

  // Clears the pending timeline function and calls its associated callback.
  void ClearPendingTimelineFunction();

  // Determines if an unrealized timeline function is currently pending.
  bool TimelineFunctionPending() {
    return pending_timeline_function_.reference_time() !=
           fuchsia::media::kUnspecifiedTime;
  }

  async_dispatcher_t* dispatcher_;
  fit::closure update_callback_;
  media::TimelineFunction current_timeline_function_;
  media::TimelineFunction pending_timeline_function_;
  int64_t end_of_stream_pts_ = fuchsia::media::kUnspecifiedTime;
  bool end_of_stream_published_ = false;
  fit::closure set_timeline_function_callback_;
  int64_t program_0_min_pts_ = std::numeric_limits<int64_t>::min();
  int64_t program_0_max_pts_ = std::numeric_limits<int64_t>::max();
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_RENDERER_H_
