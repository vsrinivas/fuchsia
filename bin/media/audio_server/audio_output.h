// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <memory>
#include <set>
#include <thread>

#include "apps/media/src/audio_server/audio_pipe.h"
#include "apps/media/src/audio_server/audio_renderer_impl.h"
#include "apps/media/src/audio_server/fwd_decls.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_point.h"

namespace media {
namespace audio {

class AudioOutput {
 public:
  virtual ~AudioOutput();

  // AddRenderer/RemoveRenderer
  //
  // Adds or removes a renderer to/from the set of current set of renderers
  // serviced by this output.  Called only from the main message loop.  Obtains
  // the processing_lock and may block for the time it takes the derived class
  // to run its processing task if the task is in progress when the method was
  // called.
  MediaResult AddRendererLink(AudioRendererToOutputLinkPtr link);
  MediaResult RemoveRendererLink(const AudioRendererToOutputLinkPtr& link);

  // Accessor for the current value of the dB gain for the output.
  float db_gain() const { return db_gain_; }

  // Set the gain for this output and update all renderer->output links.  Only
  // safe to call from the main message loop.
  void SetGain(float db_gain);

  // Accessors for the current plug state of the output.
  //
  // In addition to publishing and unpublishing streams when codecs are
  // attached/removed to/from hot pluggable buses (such as USB), some codecs
  // have the ability to detect the plugged or unplugged state of external
  // connectors (such as a 3.5mm audio jack).  Drivers can report this
  // plugged/unplugged state as well as the time of the last state change.
  // Currently this information is used in the Audio Server to implement simple
  // routing policies for AudioRenderers.
  //
  // plugged   : true when an audio output stream is either hardwired, or
  //             believes that it has something connected to its plug.
  // plug_time : The last time (according to mx_time_get(MX_CLOCK_MONOTONIC) at
  //             which the plugged/unplugged state of the output stream last
  //             changed.
  bool plugged() const { return plugged_; }
  mx_time_t plug_time() const { return plug_time_; }

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
  // AudioRenderer renderers have been disconnected.  No locks are being held.
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
  // Called on the AudioServer's main message loop any time a renderer is being
  // added to this output.  Outputs should allocate and initialize any
  // bookkeeping they will need to perform mixing on behalf of the newly added
  // renderer.
  //
  // @return MediaResult::OK if initialization succeeded, or an appropriate
  // error code otherwise.
  virtual MediaResult InitializeLink(const AudioRendererToOutputLinkPtr& link);

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
  // the process of shutting us down, unlinking us from our renderers and
  // calling the Cleanup method.
  void ShutdownSelf() FTL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // shutting_down
  //
  // Check the shutting down flag.  Only the base class may modify the flag, but
  // derived classes are free to check it at any time.
  inline bool shutting_down() const FTL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return shutting_down_;
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Other methods.
  //

  // UpdatePlugState
  //
  // Called by the audio output manager on the main message loop when it has
  // been notified of a plug state change for the output.  Used to update the
  // internal bookkeeping about the current plugged/unplugged state.  This
  // method may also be used by derived classes during Init to set an initial
  // plug state.
  //
  // Returns true if the plug state has changed, or false otherwise.
  bool UpdatePlugState(bool plugged, mx_time_t plug_time);

  // TODO(johngro): Order this by priority.  Figure out how we are going to be
  // able to quickly find a renderer with a specific priority in order to
  // optimize changes of priority.  Perhaps uniquify the priorities by assigning
  // a sequence number to the lower bits (avoiding collisions when assigning new
  // priorities will be the trick).
  //
  // Right now, we have no priorities, so this is just a set of renderer/output
  // links.
  AudioRendererToOutputLinkSet links_ FTL_GUARDED_BY(mutex_);
  AudioOutputManager* manager_;
  AudioOutputWeakPtr weak_self_;
  ftl::Mutex mutex_;

 private:
  // It's always nice when you manager is also your friend.  Seriously though,
  // the AudioOutputManager gets to call Init and Shutdown, no one else
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
  bool BeginShutdown() FTL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Called from the AudioOutputManager on the main message loop
  // thread.  Makes certain that the process of shutdown has started,
  // synchronizes with any processing tasks which were executing at the time,
  // then finishes the shutdown process by unlinking from all renderers and
  // cleaning up all resources.
  void Shutdown();

  // Called from AudioOutputManager (either directly, or indirectly from
  // Shutdown) to unlink from all AudioRenderers currently linked to this
  // output.
  void UnlinkFromRenderers();

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::thread worker_thread_;

  // Plug state is protected by the fact that it is only ever accessed on the
  // main message loop thread.
  bool plugged_ = false;
  mx_time_t plug_time_ = 0;

  // TODO(johngro): Someday, when we expose output enumeration and control from
  // the audio service, add the ability to change this value and update the
  // associated renderer-to-output-link amplitude scale factors.
  float db_gain_ = 0.0;

  // TODO(johngro): Eliminate the shutting down flag and just use the
  // task_runner_'s nullness for this test?
  volatile bool shutting_down_ FTL_GUARDED_BY(mutex_) = false;
  volatile bool shut_down_ = false;
};

}  // namespace audio
}  // namespace media
