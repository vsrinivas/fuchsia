// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_AUDIO_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_AUDIO_CLOCK_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <string>

#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class AudioClock;
class AudioClockTest;

class AudioClock {
 public:
  // There are two kinds of clocks: Client clocks (zx::clocks that clients read) and Device clocks
  // (actual clock hardware related to an audio device).
  //
  // Clock rates can change at any time. Client clock rates are changed by calls to zx_clock_update.
  // Device clock rates change intentionally (by writes to hardware controls) or unintentionally (if
  // clock hardware drifts). If AudioCore can control a clock's rate, the clock is Adjustable;
  // otherwise it is NotAdjustable.
  //
  // We describe clocks by a pair (Source, Adjustable). Source is one of {Client, Device}
  // and Adjustable is a boolean. The default constructor creates an Invalid clock, while static
  // Create methods create Client and Device clocks.
  //
  // Clock Synchronization
  // When two clocks run at slightly different rates, we error-correct to keep them synchronized.
  // This is implemented in SynchronizeClocks().
  //
  // Clock domains
  // A clock domain represents a set of clocks that always progress at the same rate (they may
  // have offsets). Adjusting a clock causes all others in the same domain to respond as one.
  // By definition, an adjustable device clock cannot be in the same clock domain as the local
  // monotonic clock (CLOCK_DOMAIN_MONOTONIC, defined in fuchsia.hardware.audio/stream.fidl),
  // because it is not strictly rate-locked to CLOCK_MONOTONIC.
  //
  // Domain is distinct from adjustability: a non-adjustable clock in a non-monotonic domain might
  // still drift relative to the local monotonic clock, even though it is not rate-adjustable.
  // AudioCore addresses hardware clock drift like any other clock misalignment (details below).
  //
  // Feedback control
  // With any clock adjustment, we cannot set the exact instant for that rate change. Adjustments
  // might overshoot or undershoot. Thus we must track POSITION (not just rate), and eliminate error
  // over time with a feedback control loop.

  static constexpr uint32_t kMonotonicDomain = fuchsia::hardware::audio::CLOCK_DOMAIN_MONOTONIC;
  static constexpr uint32_t kInvalidDomain = 0xFFFFFFFE;

  static AudioClock ClientAdjustable(zx::clock clock);
  static AudioClock ClientFixed(zx::clock clock);
  static AudioClock DeviceAdjustable(zx::clock clock, uint32_t domain);
  static AudioClock DeviceFixed(zx::clock clock, uint32_t domain);

  virtual ~AudioClock() = default;

  // No copy
  AudioClock(const AudioClock&) = delete;
  AudioClock& operator=(const AudioClock&) = delete;
  // Move is allowed
  AudioClock(AudioClock&& moved_clock) = default;
  AudioClock& operator=(AudioClock&& moved_clock) = default;

  // Returns true iff both AudioClocks refer to the same underlying zx::clock.
  bool operator==(const AudioClock& comparable) const {
    return (audio::clock::GetKoid(clock_) == audio::clock::GetKoid(comparable.clock_));
  }
  bool operator!=(const AudioClock& comparable) const { return !(*this == comparable); }

  bool is_client_clock() const { return (source_ == Source::Client); }
  bool is_device_clock() const { return (source_ == Source::Device); }
  bool is_adjustable() const { return is_adjustable_; }
  uint32_t domain() const { return domain_; }

  // Return a transform based on a snapshot of the underlying zx::clock
  virtual TimelineFunction ref_clock_to_clock_mono() const;
  virtual zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time) const;
  virtual zx::time MonotonicTimeFromReferenceTime(zx::time ref_time) const;

  fpromise::result<zx::clock, zx_status_t> DuplicateClock(
      zx_rights_t rights = ZX_RIGHT_SAME_RIGHTS) const;
  fpromise::result<zx::clock, zx_status_t> DuplicateClockReadOnly() const;
  virtual zx::time Read() const;

  // Reports whether no clock synchronization is needed (the clocks will always remain synched).
  static bool NoSynchronizationRequired(AudioClock& source_clock, AudioClock& dest_clock);

  // Reports whether clock synchronization requires a high quality resampler.
  static bool SynchronizationNeedsHighQualityResampler(AudioClock& source_clock,
                                                       AudioClock& dest_clock);

  // We synchronize audio clocks so that positions (not just rates) align, reconciling differences
  // using feedback controls. Given position error at monotonic_time, we tune the relationship
  // between these clocks so as to drive source_pos_error to 0.
  //
  // The return value is the PPM value of any micro-SRC that should subsequently be applied. This
  // must be used if neither source_clock nor dest_clock can be adjusted directly.
  static int32_t SynchronizeClocks(AudioClock& source_clock, AudioClock& dest_clock,
                                   zx::time monotonic_time, zx::duration source_pos_error);

  // Depending on the sync mode for these clocks, reset rate-adjustment appropriately.
  static void ResetRateAdjustments(AudioClock& source_clock, AudioClock& dest_clock,
                                   zx::time reset_time);

  // For debugging purposes, dump the sync mode and current clock/micro-src rates.
  static std::string SyncInfo(AudioClock& source_clock, AudioClock& dest_clock);

  // Clear internal running state and restart the feedback loop at the given time.
  void ResetRateAdjustment(zx::time reset_time);

  // Directly incorporate a position error when recovering a device clock.
  int32_t TuneForError(zx::time monotonic_time, zx::duration source_pos_error);

 protected:
  enum class Source { Client, Device };

  AudioClock(zx::clock clock, Source source, bool adjustable, uint32_t domain = kInvalidDomain);

  virtual void UpdateClockRate(int32_t rate_adjust_ppm);

 private:
  friend class AudioClockTest;

  // When tuning a ClientAdjustable to a monotonic target, we use proportional clock adjustments
  // instead of the normal PID feedback control, because once a ClientAdjustable is aligned with its
  // monotonic target, it stays aligned (the whole clock domain drifts together, if at all).
  //
  // We synchronize clocks as tightly as possible, in all sync modes; the 10-nsec error threshold
  // below is the smallest possible threshold. Clock-tuning precision is limited to integer ppms. If
  // 10 msec elapse between clock-sync measurements, a minimum rate adjustment (+/- 1ppm) will
  // change the position error (relative to monotonic) by 10 nsec. So once position error is less
  // than this threshold, we "lock" the client clock to 0 ppm.
  //
  // Note: this approach might yield acceptable results for synchronizing software clocks to
  // non-monotonic targets as well. Further investigation/measurement is needed.
  static constexpr zx::duration kLockToMonotonicErrorThreshold = zx::nsec(10);

  static constexpr int32_t kMicroSrcAdjustmentPpmMax = 2500;

  enum class SyncMode {
    // If two clocks are identical or in the same clock domain, no synchronization is needed.
    None = 0,

    // Return an adjustable clock to sync with its monotonic target, then lock to monotonic rate.
    RevertSourceToMonotonic,
    RevertDestToMonotonic,

    // We rate-adjust client clocks if they permit us, to minimize cost.
    // We also recover clocks, from devices running in non-MONOTONIC domains.
    AdjustSourceClock,
    AdjustDestClock,

    // If neither clock is adjustable, we error-correct by slightly adjusting the sample-rate
    // conversion ratio (referred to as "micro-SRC").
    MicroSrc,
  };
  static SyncMode SyncModeForClocks(AudioClock& source_clock, AudioClock& dest_clock);
  static std::string SyncModeToString(SyncMode mode);

  int32_t ClampPpm(int32_t parts_per_million);
  int32_t AdjustClock(int32_t rate_adjust_ppm);
  void LogClockAdjustments(zx::duration source_pos_error, int32_t rate_adjust_ppm);

  zx::clock clock_;

  Source source_;
  bool is_adjustable_;
  uint32_t domain_;

  audio::clock::PidControl feedback_control_;

  // State used to avoid repeated redundant zx::clock syscalls.
  int32_t current_adjustment_ppm_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_AUDIO_CLOCK_H_
