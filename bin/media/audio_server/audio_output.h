// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <memory>
#include <set>
#include <thread>

#include "apps/media/src/audio_server/audio_pipe.h"
#include "apps/media/src/audio_server/audio_track_impl.h"
#include "apps/media/src/audio_server/fwd_decls.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_point.h"

namespace mojo {
namespace media {
namespace audio {

class AudioOutput {
 public:
  virtual ~AudioOutput();

  // AddTrack/RemoveTrack
  //
  // Adds or removes a track to/from the set of current set of tracks serviced
  // by this output.  Called only from the main message loop.  Obtains the
  // processing_lock and may block for the time it takes the derived class to
  // run its processing task if the task is in progress when the method was
  // called.
  MediaResult AddTrackLink(AudioTrackToOutputLinkPtr link);
  MediaResult RemoveTrackLink(const AudioTrackToOutputLinkPtr& link);

  // Accessor for the current value of the dB gain for the output.
  float DbGain() const { return db_gain_; }

 protected:
  explicit AudioOutput(AudioOutputManager* manager);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Methods which may be implemented by derived classes to customize behavior.
  //
  //////////////////////////////////////////////////////////////////////////////

  // Init
  //
  // Called during startup on the AudioServer's main message loop thread.  No
  // locks are being held at this point.  Derived classes should allocate their
  // hardware resources and initialize any internal state.  Return
  // MediaResult::OK if everything is good and the output is ready to do work.
  virtual MediaResult Init();

  // Cleanup
  //
  // Called at shutdown on the AudioServer's main message loop thread to allow
  // derived classes to clean up any allocated resources.  All pending
  // processing callbacks have either been nerfed or run till completion.  All
  // AudioTrack tracks have been disconnected.  No locks are being held.
  virtual void Cleanup();

  // Process
  //
  // Called from within the context of the processing lock any time a scheduled
  // processing callback fires.  One callback will be automatically scheduled at
  // the end of initialization.  After that, derived classes are responsible for
  // scheduling all subsequent callbacks to keep the engine running.
  //
  // Note:  Process callbacks execute on one of the threads from the
  // AudioOutputManager's base::SequencedWorkerPool.  While successive callbacks
  // may not execute on the same thread, they are guaranteed to execute in a
  // serialized fashion.
  virtual void Process() FTL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

  // InitializeLink
  //
  // Called on the AudioServer's main message loop any time a track is being
  // added to this output.  Outputs should allocate and initialize any
  // bookkeeping they will need to perform mixing on behalf of the newly added
  // track.
  //
  // @return MediaResult::OK if initialization succeeded, or an appropriate
  // error code otherwise.
  virtual MediaResult InitializeLink(const AudioTrackToOutputLinkPtr& link);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Methods which may used by derived classes from within the context of a
  // processing callback.  Note; since these methods are intended to be called
  // from the within a process callback, the processing_lock will always be held
  // when they are called.
  //

  // ScheduleCallback
  //
  // Schedule a processing callback at the specified absolute time on the local
  // clock.
  void ScheduleCallback(ftl::TimePoint when)
      FTL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // ShutdownSelf
  //
  // Kick off the process of shooting ourselves in the head.  Note, after this
  // method has been called, no new callbacks may be scheduled.  As soon as the
  // main message loop finds out about our shutdown request, it will complete
  // the process of shutting us down, unlinking us from our tracks and calling
  // the Cleanup method.
  void ShutdownSelf();

  // shutting_down
  //
  // Check the shutting down flag.  Only the base class may modify the flag, but
  // derived classes are free to check it at any time.
  inline bool shutting_down() const FTL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return shutting_down_;
  }

  // TODO(johngro): Order this by priority.  Figure out how we are going to be
  // able to quickly find a track with a specific priority in order to optimize
  // changes of priority.  Perhaps uniquify the priorities by assigning a
  // sequence number to the lower bits (avoiding collisions when assigning new
  // priorities will be the trick).
  //
  // Right now, we have no priorities, so this is just a set of track/output
  // links.
  AudioTrackToOutputLinkSet links_;
  AudioOutputManager* manager_;

  ftl::Mutex mutex_;

 private:
  // It's always nice when you manager is also your friend.  Seriously though,
  // the AudioOutputManager gets to call Init and Shutown, no one else
  // (including derived classes) should be able to.
  friend class AudioOutputManager;

  // Thunk used to schedule delayed processing tasks on our task_runner.
  static void ProcessThunk(AudioOutputWeakPtr weak_output);

  // Called from the AudioOutputManager after an output has been created.
  // Gives derived classes a chance to set up hardware, then sets up the
  // machinery needed for scheduling processing tasks and schedules the first
  // processing callback immediately in order to get the process running.
  MediaResult Init(const AudioOutputPtr& self);

  // Called from Shutdown (main message loop) and ShutdowSelf (processing
  // context).  Starts the process of shutdown, preventing new processing tasks
  // from being scheduled, and nerfing any tasks in flight.
  //
  // @return true if this call just kicked off the process of shutting down,
  // false otherwise.
  bool BeginShutdown();

  // Called from the AudioOutputManager on the main message loop
  // thread.  Makes certain that the process of shutdown has started,
  // synchronizes with any processing tasks which were executing at the time,
  // then finishes the shutdown process by unlinking from all tracks and
  // cleaning up all resources.
  void Shutdown();

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  AudioOutputWeakPtr weak_self_;
  std::thread worker_thread_;

  // TODO(johngro): Someday, when we expose output enumeration and control
  // from
  // the audio service, add the ability to change this value and update the
  // assocated track-to-output-link amplitude scale factors.
  float db_gain_ = 0.0;

  // TODO(johngro): Eliminate the shutting down flag and just use the
  // task_runner_'s nullness for this test?
  volatile bool shutting_down_ FTL_GUARDED_BY(mutex_) = false;
  volatile bool shut_down_ = false;
};

}  // namespace audio
}  // namespace media
}  // namespace mojo
