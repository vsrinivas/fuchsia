// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_SETTINGS_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_SETTINGS_H_

#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {
namespace audio {

class AudioDriver;

class AudioDeviceSettings : public fbl::RefCounted<AudioDeviceSettings> {
 public:
  struct GainState {
    float db_gain = 0.0f;
    bool muted = false;
    bool agc_enabled = false;
  };

  static fbl::RefPtr<AudioDeviceSettings> Create(const AudioDriver& drv) {
    return fbl::AdoptRef(new AudioDeviceSettings(drv));
  }

  // Disallow copy/move construction/assignment
  AudioDeviceSettings(const AudioDeviceSettings&) = delete;
  AudioDeviceSettings(AudioDeviceSettings&&) = delete;
  AudioDeviceSettings& operator=(const AudioDeviceSettings&) = delete;
  AudioDeviceSettings& operator=(AudioDeviceSettings&&) = delete;

  //////////////////////////////////////////////////////////////////////////////
  //
  // Begin accessors used only from the AudioDeviceManager
  //
  //////////////////////////////////////////////////////////////////////////////

  // SetGainInfo
  // Update the internal gain state using the supplied FIDL gain info structure,
  // and return true if there was a meaningful change to the internal gain
  // state which would warrant waking up the AudioDevice.  Otherwise, return
  // false.
  bool SetGainInfo(const ::fuchsia::media::AudioGainInfo& info,
                   uint32_t set_flags) FXL_LOCKS_EXCLUDED(settings_lock_);

  // GetGainInfo
  // Fetch a copy of the current gain state packed into a FIDL structure
  // suitable for reporting gain state.
  void GetGainInfo(::fuchsia::media::AudioGainInfo* out_info) const
      FXL_LOCKS_EXCLUDED(settings_lock_);

  //////////////////////////////////////////////////////////////////////////////
  //
  // End accessors used only from the AudioDeviceManager
  //
  //////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////////
  //
  // Begin accessors used only from the AudioDevice's mix domain.
  //
  //////////////////////////////////////////////////////////////////////////////

  // Snapshot the current gain state and return flags which indicate which of
  // the gain settings have changed since the last observation.
  audio_set_gain_flags_t SnapshotGainState(GainState* out_state)
      FXL_LOCKS_EXCLUDED(settings_lock_);

  //////////////////////////////////////////////////////////////////////////////
  //
  // End accessors used only from the AudioDevice's mix domain.
  //
  //////////////////////////////////////////////////////////////////////////////

 private:
  explicit AudioDeviceSettings(const AudioDriver& drv);

  const audio_stream_unique_id_t uid_;
  const bool can_mute_;
  const bool can_agc_;

  // The settings_lock_ protects any settings state which needs to be set by the
  // AudioDeviceManager and observed atomically by the mix domain threads.  Any
  // state which is used only by the AudioDeviceManager, or which can be
  // observed using std::atomic<>, does not need to be protected by the
  // settings_lock_.
  mutable fbl::Mutex settings_lock_;
  GainState gain_state_ FXL_GUARDED_BY(settings_lock_);
  audio_set_gain_flags_t gain_state_dirty_flags_
      FXL_GUARDED_BY(settings_lock_) = static_cast<audio_set_gain_flags_t>(0);
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_SETTINGS_H_
