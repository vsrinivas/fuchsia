// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <mutex>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/syslog/cpp/logger.h"

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

  static fbl::RefPtr<AudioDeviceSettings> Create(const AudioDriver& drv, bool is_input);

  AudioDeviceSettings(const audio_stream_unique_id_t& uid, const HwGainState& hw, bool is_input);

  // Initialize this object with the contents of another instance with the same unique
  // id. Do not make any attempt to persist these settings to disk from now on.
  void InitFromClone(const AudioDeviceSettings& other);
  fbl::RefPtr<AudioDeviceSettings> Clone();

  // Simple accessors for constant properties
  const audio_stream_unique_id_t& uid() const { return uid_; }
  bool is_input() const { return is_input_; }

  // Simple accessors for persisted properties
  bool Ignored() const { return ignored_; }
  void SetIgnored(bool ignored);
  bool AutoRoutingDisabled() const { return auto_routing_disabled_; }
  void SetAutoRoutingDisabled(bool auto_routing_disabled);

  void set_observer(fit::function<void(const AudioDeviceSettings*)> observer) {
    FX_DCHECK(!observer_);
    observer_ = std::move(observer);
  }

  // Disallow move construction/assignment and copy assign.
  // We keep a private copy ctor to assist in implementing the |Clone| operation.
  AudioDeviceSettings(AudioDeviceSettings&&) = delete;
  AudioDeviceSettings& operator=(const AudioDeviceSettings&) = delete;
  AudioDeviceSettings& operator=(AudioDeviceSettings&&) = delete;

  //////////////////////////////////////////////////////////////////////////////
  //
  // Begin accessors used only from the AudioDeviceManager
  //
  //////////////////////////////////////////////////////////////////////////////

  // SetGainInfo
  // Update the internal gain state using the supplied FIDL gain info structure. Return true if
  // a meaningful change occurred (this warrants waking up the AudioDevice), else return false.
  bool SetGainInfo(const fuchsia::media::AudioGainInfo& info, uint32_t set_flags)
      FXL_LOCKS_EXCLUDED(settings_lock_);

  // GetGainInfo
  // Fetch a copy of current gain state packed into a FIDL structure suitable for notifications.
  void GetGainInfo(fuchsia::media::AudioGainInfo* out_info) const
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

  // Snapshot current gain state. Return flags indicating which settings changed since last time.
  audio_set_gain_flags_t SnapshotGainState(GainState* out_state) FXL_LOCKS_EXCLUDED(settings_lock_);

  //////////////////////////////////////////////////////////////////////////////
  //
  // End accessors used only from the AudioDevice's mix domain.
  //
  //////////////////////////////////////////////////////////////////////////////

 private:
  AudioDeviceSettings(const AudioDeviceSettings& o);

  void NotifyObserver();

  const audio_stream_unique_id_t uid_;
  const bool is_input_;
  const bool can_agc_;

  // These should only be accessed from the AudioDeviceManager's message loop thread.
  bool ignored_ = false;
  bool auto_routing_disabled_ = false;

  // The settings_lock_ protects any settings state which needs to be set by the AudioDeviceManager
  // and observed atomically by the mix domain threads. Any state which is used only by the
  // AudioDeviceManager, or which can be observed using std::atomic<>, does not need to be protected
  // by the settings_lock_.
  mutable std::mutex settings_lock_;
  GainState gain_state_ FXL_GUARDED_BY(settings_lock_);
  audio_set_gain_flags_t gain_state_dirty_flags_ FXL_GUARDED_BY(settings_lock_) =
      static_cast<audio_set_gain_flags_t>(0);

  fit::function<void(const AudioDeviceSettings*)> observer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_H_
