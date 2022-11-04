// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DEVICE_SETTINGS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DEVICE_SETTINGS_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/device/audio.h>

#include <mutex>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace media::audio {

class AudioDevice;
class AudioDriver;
struct HwGainState;

class AudioDeviceSettings : public fbl::RefCounted<AudioDeviceSettings> {
 public:
  struct GainState {
    float gain_db = 0.0f;
    //  TODO(mpuryear): make this true, consistent w/driver_output.cc?
    bool muted = false;
    bool agc_enabled = false;
  };

  AudioDeviceSettings(const audio_stream_unique_id_t& uid, const HwGainState& hw, bool is_input);

  // Disallow copy/move.
  AudioDeviceSettings(const AudioDeviceSettings& o);
  AudioDeviceSettings(AudioDeviceSettings&&) = delete;
  AudioDeviceSettings& operator=(const AudioDeviceSettings&) = delete;
  AudioDeviceSettings& operator=(AudioDeviceSettings&&) = delete;

  // Simple accessors for constant properties
  const audio_stream_unique_id_t& uid() const { return uid_; }
  bool is_input() const { return is_input_; }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Begin accessors used only from the AudioDeviceManager
  //
  //////////////////////////////////////////////////////////////////////////////

  // SetGainInfo
  // Update the internal gain state using the supplied FIDL gain info structure. Return true if
  // a meaningful change occurred (this warrants waking up the AudioDevice), else return false.
  bool SetGainInfo(const fuchsia::media::AudioGainInfo& info,
                   fuchsia::media::AudioGainValidFlags set_flags)
      FXL_LOCKS_EXCLUDED(settings_lock_);

  // GetGainInfo
  // Fetch a copy of current gain state packed into a FIDL structure suitable for notifications.
  fuchsia::media::AudioGainInfo GetGainInfo() const FXL_LOCKS_EXCLUDED(settings_lock_);

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

  // Snapshot current gain state. Return flags indicating which settings changed since last time.
  std::pair<audio_set_gain_flags_t, AudioDeviceSettings::GainState> SnapshotGainState()
      FXL_LOCKS_EXCLUDED(settings_lock_);

  //////////////////////////////////////////////////////////////////////////////
  //
  // End accessors used only from the AudioDevice's mix domain.
  //
  //////////////////////////////////////////////////////////////////////////////

 private:
  const audio_stream_unique_id_t uid_;
  const bool is_input_;
  const bool can_agc_;

  // The settings_lock_ protects any settings state which needs to be set by the AudioDeviceManager
  // and observed atomically by the mix domain threads. Any state which is used only by the
  // AudioDeviceManager, or which can be observed using std::atomic<>, does not need to be protected
  // by the settings_lock_.
  mutable std::mutex settings_lock_;
  GainState gain_state_ FXL_GUARDED_BY(settings_lock_);
  audio_set_gain_flags_t gain_state_dirty_flags_ FXL_GUARDED_BY(settings_lock_) =
      static_cast<audio_set_gain_flags_t>(0);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DEVICE_SETTINGS_H_
