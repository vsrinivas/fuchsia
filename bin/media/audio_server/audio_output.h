// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_OUTPUT_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_OUTPUT_H_

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
  // Minimum clock lead time (in nanoseconds) for this output
  int64_t min_clock_lead_time_nsec() const { return min_clock_lead_time_nsec_; }

 protected:
  explicit AudioOutput(AudioDeviceManager* manager);

  int64_t min_clock_lead_time_nsec_ = 0;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_OUTPUT_H_
