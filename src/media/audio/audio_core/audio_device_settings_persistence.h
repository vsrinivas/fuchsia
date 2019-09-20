// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_PERSISTENCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_PERSISTENCE_H_

#include <lib/async/cpp/task.h>
#include <lib/trace/event.h>

#include <map>

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/fxl/synchronization/thread_checker.h"
#include "src/media/audio/audio_core/audio_device_settings.h"
#include "src/media/audio/audio_core/audio_device_settings_serialization.h"
#include "src/media/audio/audio_core/threading_model.h"

namespace media::audio {

class AudioDeviceSettingsPersistence {
 public:
  struct ConfigSource {
    const std::string& prefix;
    bool is_default;
  };

  static constexpr zx::duration kMaxUpdateDelay = zx::sec(5);
  static constexpr zx::duration kUpdateDelay = zx::msec(500);
  static std::unique_ptr<AudioDeviceSettingsSerialization> CreateDefaultSettingsSerializer();

  explicit AudioDeviceSettingsPersistence(ThreadingModel* threading_model);

  // Construct an |AudioDeviceSettingsPersistence| with custom config sources. Primarily intended
  // for testing purposes. Production use cases should be able to use the single-arg constructor
  // which uses the default config sources.
  //
  // The array reference must be guaranteed to outlive this instance as an internal reference will
  // be retained.
  AudioDeviceSettingsPersistence(ThreadingModel* threading_model,
                                 std::unique_ptr<AudioDeviceSettingsSerialization> serialization,
                                 const AudioDeviceSettingsPersistence::ConfigSource (&configs)[2]);

  // Disallow copy and move.
  AudioDeviceSettingsPersistence(const AudioDeviceSettingsPersistence&) = delete;
  AudioDeviceSettingsPersistence(AudioDeviceSettingsPersistence&&) = delete;
  AudioDeviceSettingsPersistence& operator=(const AudioDeviceSettingsPersistence&) = delete;
  AudioDeviceSettingsPersistence& operator=(AudioDeviceSettingsPersistence&&) = delete;

  // Enables or disables writing settings to back to disk.
  void EnableDeviceSettings(bool enabled) { writes_enabled_.store(enabled); }

  // Loads any state for |settings| from disk if it exists. This method will pass |settings| to an
  // alternate thread to read from disk and |settings| will be in an undefined state until the
  // returned promise resolves.
  //
  // Once loaded, |settings| will have an attached observer that will automatically handle writing
  // changes back to disk, possibly with a delay. To ensure settings are in a consistent state with
  // the disk, see |FinalizeSettings|.
  //
  // The returned promise must be scheduled on the FIDL thread executor, the promise will ensure
  // disk operations are still scheduled on the IO thread.
  fit::promise<void, zx_status_t> LoadSettings(fbl::RefPtr<AudioDeviceSettings> settings);

  // Immediately schedules a write-back for |settings| if the settings are known to be dirty but a
  // write-back has not yet been performed. The returned promise will be completed when the disk
  // write has been completed.
  //
  // The returned promise must be scheduled on the FIDL thread executor, the promise will ensure
  // disk operations are still scheduled on the IO thread.
  fit::promise<void, zx_status_t> FinalizeSettings(const AudioDeviceSettings& settings);

 private:
  // Holds any state associated with an |AudioDeviceSettings| instance that has been loaded from
  // disk.
  struct AudioDeviceSettingsHolder {
    AudioDeviceSettingsHolder(fbl::RefPtr<AudioDeviceSettings> _settings, fbl::unique_fd _storage)
        : settings(std::move(_settings)), storage(std::move(_storage)) {}

    fbl::RefPtr<AudioDeviceSettings> settings;
    fbl::unique_fd storage;
    trace_async_id_t nonce = TRACE_NONCE();

    // Members which control the dirty/clean status of the settings relative to storage, and which
    // control the Nagle-ish commit limiter.
    //
    // We introduce two absolute timeouts, a next commit time that is tracked as the scheduled
    // deadline for |commit_task|, and |max_commit_time|. When settings are clean (in sync with
    // storage), |commit_task.is_pending()| will ben false and |max_commit_time| will be
    // |zx::time::infinite()|. Anytime a change is introduced, the timeouts are updated as follows:
    //
    // 1) If |max_commit_time| is infinite, it is set to now + MaxUpdateDelay, otherwise it is
    //    unchanged.
    // 2) |commit_task| gets (re)scheduled to min(now + UpdateDelay, max_commit_time)
    //
    // The general idea here is to wait a short amount of time before committing the settings to
    // storage, because another change may be arriving very soon. This said, if the settings are
    // constantly changing, they will need to eventually be committed. The UpdateDelay determines
    // the maximum possible rate at which the settings will be committed, while MaxUpdateDelay
    // determines the minimum commit rate in the event that the settings are constantly changing.
    zx::time max_commit_time = zx::time::infinite();
    async::Task commit_task;
  };

  // Simply runs |ReadSettingsFromDiskBlocking| on the IO dispatcher and returns a promise that
  // completes with the result of that operation.
  fit::promise<fbl::unique_fd, zx_status_t> ReadSettingsFromDisk(
      fbl::RefPtr<AudioDeviceSettings> settings);

  fit::result<fbl::unique_fd, zx_status_t> ReadSettingsFromDiskBlocking(
      fbl::RefPtr<AudioDeviceSettings> settings)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(threading_model_.IoDomain().token());

  fit::promise<void, zx_status_t> Commit(fbl::RefPtr<AudioDeviceSettings> settings, int fd,
                                         trace_async_id_t nonce);

  zx_status_t WriteSettingsToFile(fbl::RefPtr<AudioDeviceSettings> settings, int fd)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(threading_model_.IoDomain().token());

  void RescheduleCommitTask(AudioDeviceSettingsHolder* holder);

  fxl::ThreadChecker thread_checker_;

  const ConfigSource (&configs_)[2];
  std::atomic<bool> writes_enabled_{true};
  struct SettingsMapCompare {
    constexpr bool operator()(const AudioDeviceSettings* k1, const AudioDeviceSettings* k2) const {
      return (k1->is_input() && !k2->is_input()) ||
             ((k1->is_input() == k2->is_input()) &&
              (memcmp(&k1->uid(), &k2->uid(), sizeof(k1->uid())) < 0));
    }
  };
  std::map<const AudioDeviceSettings*, AudioDeviceSettingsHolder, SettingsMapCompare>
      persisted_device_settings_ FXL_GUARDED_BY(thread_checker_);
  ThreadingModel& threading_model_;
  std::unique_ptr<AudioDeviceSettingsSerialization> serialization_
      FXL_GUARDED_BY(threading_model_.IoDomain().token());
};

}  // namespace media::audio
#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_PERSISTENCE_H_
