// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_SETTINGS_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_SETTINGS_H_

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fuchsia/media/cpp/fidl.h>
#include <rapidjson/schema.h>
#include <zircon/device/audio.h>

#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {
namespace audio {

class AudioDevice;
class AudioDriver;

class AudioDeviceSettings
    : public fbl::RefCounted<AudioDeviceSettings>,
      public fbl::WAVLTreeContainable<fbl::RefPtr<AudioDeviceSettings>> {
 public:
  struct GainState {
    float db_gain = 0.0f;
    bool muted = false;
    bool agc_enabled = false;
  };

  static fbl::RefPtr<AudioDeviceSettings> Create(const AudioDriver& drv,
                                                 bool is_input) {
    return fbl::AdoptRef(new AudioDeviceSettings(drv, is_input));
  }

  static void Initialize();

  // Initialize the contents of this audio driver structure from persisted
  // settings on disk, or (if that fails) create a new settings file with the
  // current initial settings.
  zx_status_t InitFromDisk();

  // Clone the contents of this AudioDeviceSettings from a different
  // AudioDeviceSettings instance with the same unique id.  Do not make any
  // attempt to persist these settings to disk from now on.
  void InitFromClone(const AudioDeviceSettings& other);

  // Commit dirty setttings to storage if needed, and return the next time at
  // which we should commit our settings, or zx::time::infinite() if the
  // settings are now clean and do not need to be commited in the future.
  zx::time Commit(bool force = false);

  // Simple accessors for constant properties
  const audio_stream_unique_id_t& uid() const { return uid_; }
  bool is_input() const { return is_input_; }

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
  AudioDeviceSettings(const AudioDriver& drv, bool is_input);

  zx_status_t Deserialize();
  zx_status_t Serialize();
  void UpdateCommitTimeouts();
  void CancelCommitTimeouts();

  static const std::string kSettingsPath;
  static bool initialized_;
  static std::unique_ptr<rapidjson::SchemaDocument> file_schema_;

  const audio_stream_unique_id_t uid_;
  const bool is_input_;
  const bool can_mute_;
  const bool can_agc_;

  // Members which should only ever be accessed from the context of the
  // AudioDeviceManager's message loop thread.
  fbl::unique_fd storage_;

  // Members which control the dirty/clean status of the settings relative to
  // storage, and which control the Nagle-ish commit limiter.
  //
  // We introduce two absolute timeouts, next_commit_time and max_commit_time.
  // When settings are clean (in sync with storage), both will be infinite.
  // Anytime a change is introduced, the timeouts are updated as follows.
  //
  // 1) If max is infinite, it gets set to now + MaxUpdateDelay, otherwise it is
  //    unchanged.
  // 2) next gets set to min(now + UpdateDelay, max_commit_time)
  //
  // When now >= next, it is time to commit.  The general idea here is to wait a
  // short amount of time before committing the settings to storage, because
  // another change may be arriving very soon.  This said, if the settings are
  // constantly changing, they will need to eventually be commited.  The
  // UpdateDelay determines the maximum possible rate at which the settings will
  // be commited, while MaxUpdateDelay determines the minimum commit rate in the
  // event that the settings are constantly changing.
  zx::time next_commit_time_ = zx::time::infinite();
  zx::time max_commit_time_ = zx::time::infinite();

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
