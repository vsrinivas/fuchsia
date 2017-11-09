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
  // Accessor for the current value of the dB gain for the output.
  float db_gain() const { return db_gain_.load(std::memory_order_acquire); }

  // Set the gain for this output.
  void SetGain(float db_gain) {
    db_gain_.store(db_gain, std::memory_order_release);
  }

 protected:
  explicit AudioOutput(AudioDeviceManager* manager);

 private:
  // TODO(johngro): Someday, when we expose output enumeration and control from
  // the audio service, add the ability to change this value and update the
  // associated renderer-to-output-link amplitude scale factors.
  std::atomic<float> db_gain_;
};

}  // namespace audio
}  // namespace media
