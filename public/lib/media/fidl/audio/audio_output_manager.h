// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_AUDIO_AUDIO_OUTPUT_MANAGER_H_
#define SERVICES_MEDIA_AUDIO_AUDIO_OUTPUT_MANAGER_H_

#include <set>

#include "base/synchronization/lock.h"
#include "base/threading/sequenced_worker_pool.h"
#include "mojo/services/media/common/interfaces/media_common.mojom.h"
#include "mojo/services/media/common/interfaces/media_transport.mojom.h"
#include "services/media/audio/audio_output.h"
#include "services/media/audio/fwd_decls.h"

namespace mojo {
namespace media {
namespace audio {

class AudioOutputManager {
 public:
  explicit AudioOutputManager(AudioServerImpl* server);
  ~AudioOutputManager();

  // Initialize the output manager.  Called from the service implementation,
  // once, at startup time.  Should...
  //
  // 1) Initialize the mixing thread pool.
  // 2) Instantiate all of the built-in audio output devices.
  // 3) Being monitoring for plug/unplug events for pluggable audio output
  //    devices.
  MediaResult Init();

  // Blocking call.  Called by the service, once, when it is time to shutdown
  // the service implementation.  While this function is blocking, it must never
  // block for long.  Our process is going away; this is our last chance to
  // perform a clean shutdown.  If an unclean shutdown must be performed in
  // order to implode in a timely fashion, so be it.
  //
  // Shutdown must be idempotent, and safe to call from the output manager's
  // destructor, although it should never be necessary to do so.  If the
  // shutdown called from the destructor has to do real work, something has gone
  // Very Seriously Wrong.
  void Shutdown();

  // Select the initial set of outputs for a track which has just been
  // configured.
  void SelectOutputsForTrack(AudioTrackImplPtr track);

  // Schedule a closure to run on our encapsulating server's main message loop.
  void ScheduleMessageLoopTask(const tracked_objects::Location& from_here,
                               const base::Closure& task);

  // Shutdown the specified audio output and remove it from the set of active
  // outputs.
  void ShutdownOutput(AudioOutputPtr output);

 private:
  void CreateAlsaOutputs();

  // TODO(johngro): A SequencedWorkerPool currently seems to be as close to what
  // we want which we can currently get using the chrome/mojo framework.  Things
  // which are missing and will eventually need to be addressed include...
  //
  // 1) Threads are created on the fly, as needed.  We really want to be able to
  //    spin up the proper number of threads at pool creation time.  Audio
  //    mixing is very timing sensitive...  If we are in a situation where we
  //    have not hit the max number of threads in the pool, and we need to spin
  //    up a thread in order to mix, we *really* do not want to have to wait to
  //    create the thread at the OS level before we can mix some audio.  The
  //    thread needs to already be ready to go.
  // 2) Threads in the pool are created with default priority.  Audio mixing
  //    threads will need to be created with elevated priority.
  // 3) It is currently unclear if explicitly scheduling tasks with delays will
  //    be sufficient for the audio mixer.  We really would like to be able to
  //    have tasks fire when some handle abstraction becomes signalled.  This
  //    will let us implement mixing not only with open loop timing, but also
  //    with events which can come from device drivers.  Note: this seems to be
  //    an issue with the TaskRunner architecture in general, not the
  //    SequencedWorkerPool in specific.
  // 4) The resolution of posted delayed tasks may hinder the low latency goals
  //    of the system.  Being able to know the underlying achievable resolution
  //    of dispatching delayed tasks is a minimum requirement.  From that, we
  //    can compute our worst case overhead which can be communicated to the
  //    user and will have an effect on overall latency.  Hitting something on
  //    the order of 10s of microseconds is what we really should be shooting
  //    for here.  Single milliseconds is probably too coarse.
  // 5) Not a requirement, but a sure-would-be-nice-to-have... Scheduling
  //    delayed tasks using absolute times.  Having to schedule using delta
  //    times from now means that we need to take the time it takes to schedule
  //    the task (along with its jitter) into account when scheduling.  This can
  //    lead to additional, undesirable, latency.
  scoped_refptr<base::SequencedWorkerPool> thread_pool_;

  // A pointer to the server which encapsulates us.  It is not possible for this
  // pointer to be bad while we still exist.
  AudioServerImpl* server_;

  // Our set of currently active audio output instances.
  //
  // Contents of the output set must only be manipulated on the main message
  // loop thread, so no synchronization should be needed.
  AudioOutputSet outputs_;
};

}  // namespace audio
}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_AUDIO_AUDIO_OUTPUT_MANAGER_H_
