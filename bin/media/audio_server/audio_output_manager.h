// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <set>

#include "lib/media/fidl/media_result.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"
#include "garnet/bin/media/audio_server/audio_output.h"
#include "garnet/bin/media/audio_server/audio_plug_detector.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"

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

  // Add a renderer to the set of active audio renderers.
  void AddRenderer(AudioRendererImplPtr renderer) {
    FXL_DCHECK(renderer);
    renderers_.insert(std::move(renderer));
  }

  // Remove a renderer from the set of active audio renderers.
  void RemoveRenderer(AudioRendererImplPtr renderer) {
    size_t removed = renderers_.erase(renderer);
    FXL_DCHECK(removed);
  }

  // Select the initial set of outputs for a renderer which has just been
  // configured.
  void SelectOutputsForRenderer(AudioRendererImplPtr renderer);

  // Link an output to an audio renderer
  void LinkOutputToRenderer(AudioOutputPtr output,
                            AudioRendererImplPtr renderer);

  // Schedule a closure to run on our encapsulating server's main message loop.
  void ScheduleMessageLoopTask(const fxl::Closure& task);

  // Attempt to initialize an output and add it to the set of active outputs.
  MediaResult AddOutput(AudioOutputPtr output);

  // Shutdown the specified audio output and remove it from the set of active
  // outputs.
  void ShutdownOutput(AudioOutputPtr output);

  // Handles a plugged/unplugged state change for the supplied audio output.
  void HandlePlugStateChange(AudioOutputPtr output,
                             bool plugged,
                             mx_time_t plug_time);

  // Master gain control.  Only safe to access via the main message loop thread.
  void  SetMasterGain(float db_gain);
  float master_gain() const { return master_gain_; }

 private:
  // A placeholder for various types of simple routing policies.  This should be
  // replaced when routing policy moves to a more centralized policy manager.
  enum class RoutingPolicy {
    // AudioRenderers are always connected to all audio outputs which currently
    // in the plugged state (eg; have a connector attached to them)
    ALL_PLUGGED_OUTPUTS,

    // AudioRenderers are only connected to the output stream which most
    // recently entered the plugged state.  Renderers move around from output to
    // output as streams are published/unpublished and become plugged/unplugged.
    LAST_PLUGGED_OUTPUT,
  };

  // Find the last plugged (non-throttle_output) active output in the system, or
  // nullptr if none of the outputs are currently plugged.
  AudioOutputPtr FindLastPluggedOutput();

  // Methods for dealing with routing policy when an output becomes unplugged or
  // completely removed from the system, or has become plugged/newly added to
  // the system.
  void OnOutputUnplugged(AudioOutputPtr output);
  void OnOutputPlugged(AudioOutputPtr output);

  // A pointer to the server which encapsulates us.  It is not possible for this
  // pointer to be bad while we still exist.
  AudioServerImpl* server_;

  // Our sets of currently active audio outputs and renderers.
  //
  // Contents of these collections must only be manipulated on the main message
  // loop thread, so no synchronization should be needed.
  AudioOutputSet outputs_;
  AudioRendererImplSet renderers_;

  // The special throttle output.  This output always exists, and is always used
  // by all renderers.
  AudioOutputPtr throttle_output_;

  // A helper class we will use to detect plug/unplug events for audio devices
  AudioPlugDetector plug_detector_;

  // Current master gain setting (in dB).
  //
  // TODO(johngro): remove this when we have a policy manager which controls
  // gain on a per-output basis.
  float master_gain_ = -20.0;

  RoutingPolicy routing_policy_ = RoutingPolicy::LAST_PLUGGED_OUTPUT;
};

}  // namespace audio
}  // namespace media
