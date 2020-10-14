// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/format/frames.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class AudioClock;

namespace audio_clock_helper {
const zx::clock& get_underlying_zx_clock(const AudioClock&);
}

class AudioClockTest;

class AudioClock {
 public:
  // There are two kinds of clocks: Client clocks (zx::clocks that clients read) and Device clocks
  // (actual clock hardware related to an audio device).
  //
  // Clock rates can change at any time. Client clock rates are changed by calls to zx_clock_update.
  // Device clock rates are changed by writes to hardware controls, or if clock hardware drifts. If
  // AudioCore can control a clock's rate, the clock is Adjustable; otherwise it is NotAdjustable.
  //
  // We describe clocks by a pair (Source, Adjustable). Source is one of {Client, Device}
  // and Adjustable is a boolean. The default constructor creates an Invalid clock, while static
  // Create methods create Client and Device clocks.
  //
  // Clock Synchronization
  // When two clocks run at slightly different rates, we error-correct to keep them synchronized.
  // Exactly how we do this depends on their respective types, as explained by the output of the
  // SynchronizationMode() function.
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

  static AudioClock CreateAsClientAdjustable(zx::clock clock);
  static AudioClock CreateAsClientNonadjustable(zx::clock clock);
  static AudioClock CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain);
  static AudioClock CreateAsDeviceNonadjustable(zx::clock clock, uint32_t domain);

  static Mixer::Resampler UpgradeResamplerIfNeeded(Mixer::Resampler resampler_hint,
                                                   AudioClock& source_clock,
                                                   AudioClock& dest_clock);

  AudioClock() = delete;
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

  bool controls_device_clock() const { return controls_device_clock_; }
  void set_controls_device_clock(bool should_control_device_clock);

  // Return a transform based on a snapshot of the underlying zx::clock
  TimelineFunction ref_clock_to_clock_mono() const;
  zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time) const;
  zx::time MonotonicTimeFromReferenceTime(zx::time ref_time) const;

  zx::clock DuplicateClock() const;
  zx::time Read() const;

  // Audio clocks use a PID control; across a series of external rate adjustments, we track position
  // (not just rate). Inputs to the feedback control are destination frame as "time" component, and
  // source position error (in frac frames) as the process variable which we tune to zero. We
  // regularly tune the feedback by reporting current position error at a given time; the PID
  // provides the rate adjustment to be applied.
  //
  // At a given dest_frame, the source position error is provided (in fractional frames). This is
  // used to maintain an adjustment_rate() factor that eliminates the error over time.
  static int32_t SynchronizeClocks(AudioClock& source_clock, AudioClock& dest_clock,
                                   Fixed frac_src_error, int64_t dest_frame);

  // Clear internal running state and restart the feedback loop at the given destination frame.
  virtual void ResetRateAdjustment(int64_t dest_frame);

 private:
  friend const zx::clock& audio_clock_helper::get_underlying_zx_clock(const AudioClock&);
  friend class ::media::audio::AudioClockTest;

  enum class Source { Client, Device };
  enum class SyncMode {
    // If two clocks are identical or in the same clock domain, no synchronization is needed.
    None,

    // We rate-adjust zx::clocks even if hardware is also adjustable, to minimize disruption.
    AdjustSourceClock,
    AdjustDestClock,

    // If clock hardware is adjustable, AudioCore may adjust it to follow a client clock; if so,
    // this client clock is considered "hardware-controlling". If an adjustable device clock has no
    // "controlling" client clock, it is treated as not adjustable.
    AdjustSourceHardware,
    AdjustDestHardware,

    // If neither clock is adjustable, we error-correct by slightly adjusting the sample-rate
    // conversion ratio (referred to as "micro-SRC"). This can occur with an adjustable device
    // clock, if another stream's client clock is already controlling that device clock hardware.
    MicroSrc,
  };
  static SyncMode SynchronizationMode(AudioClock& source_clock, AudioClock& dest_clock);
  static int32_t ClampPpm(SyncMode sync_mode, int32_t parts_per_million);

  static constexpr int32_t kMicroSrcAdjustmentPpmMax = 2500;

  AudioClock(zx::clock clock, Source source, bool adjustable)
      : AudioClock(std::move(clock), source, adjustable, kInvalidDomain) {}
  AudioClock(zx::clock clock, Source source, bool adjustable, uint32_t domain);

  zx::clock clock_;

  Source source_;
  bool is_adjustable_;
  uint32_t domain_;
  int32_t adjustment_ppm_;
  bool controls_device_clock_;

  audio::clock::PidControl microsrc_feedback_control_;
  std::optional<audio::clock::PidControl> adjustable_feedback_control_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
