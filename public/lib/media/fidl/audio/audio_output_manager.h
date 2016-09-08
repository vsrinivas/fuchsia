// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_AUDIO_AUDIO_OUTPUT_MANAGER_H_
#define APPS_MEDIA_SERVICES_AUDIO_AUDIO_OUTPUT_MANAGER_H_

#include <set>

#include "apps/media/interfaces/media_common.mojom.h"
#include "apps/media/interfaces/media_transport.mojom.h"
#include "apps/media/services/audio/audio_output.h"
#include "apps/media/services/audio/fwd_decls.h"

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
  void ScheduleMessageLoopTask(const ftl::Closure& task);

  // Shutdown the specified audio output and remove it from the set of active
  // outputs.
  void ShutdownOutput(AudioOutputPtr output);

 private:
  void CreateAlsaOutputs();

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

#endif  // APPS_MEDIA_SERVICES_AUDIO_AUDIO_OUTPUT_MANAGER_H_
