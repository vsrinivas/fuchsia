// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <memory>
#include <set>
#include <thread>

#include "garnet/bin/media/audio_server/audio_device.h"
#include "garnet/bin/media/audio_server/audio_driver.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {
namespace audio {

class DriverRingBuffer;

class AudioOutput : public AudioDevice {
 public:
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
  float db_gain() const { return db_gain_.load(std::memory_order_acquire); }

  // Set the gain for this output.
  void SetGain(float db_gain) {
    db_gain_.store(db_gain, std::memory_order_release);
  }

 protected:
  explicit AudioOutput(AudioDeviceManager* manager);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Methods which may be implemented by derived classes to customize behavior.
  //
  //////////////////////////////////////////////////////////////////////////////

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

  // Implementation of AudioDevice method, called from AudioDeviceManager
  // (either directly, or indirectly from Shutdown).  Unlinks from all
  // AudioRenderers currently linked to this output.
  void Unlink() override;

  // TODO(johngro): Order this by priority.  Figure out how we are going to be
  // able to quickly find a renderer with a specific priority in order to
  // optimize changes of priority.  Perhaps uniquify the priorities by assigning
  // a sequence number to the lower bits (avoiding collisions when assigning new
  // priorities will be the trick).
  //
  // Right now, we have no priorities, so this is just a set of renderer/output
  // links.
  AudioRendererToOutputLinkSet links_ FXL_GUARDED_BY(mutex_);

 private:
  // TODO(johngro): Someday, when we expose output enumeration and control from
  // the audio service, add the ability to change this value and update the
  // associated renderer-to-output-link amplitude scale factors.
  std::atomic<float> db_gain_;
};

}  // namespace audio
}  // namespace media
