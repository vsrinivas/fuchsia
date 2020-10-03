// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <limits>

#include "src/media/audio/lib/clock/pid_control.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/format/frames.h"

namespace media::audio {

class AudioClock;

namespace audio_clock_helper {
const zx::clock& get_underlying_zx_clock(const AudioClock&);
}

class AudioClock {
 public:
  // There are two kinds of clocks: Client clocks (zx::clocks that clients read) and Device clocks
  // (actual clock hardware related to an audio device).
  //
  // Clock rates can change at any time. Client clock rates are changed by calls to zx_clock_update.
  // Device clock rates are changed by writes to hardware controls, or if clock hardware drifts. If
  // AudioCore can control a clock's rate, the clock is Adjustable; otherwise it is NotAdjustable.
  //
  // We describe clocks by a pair (Source, Adjustable). Source is one of {Client, Device, Invalid}
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

  AudioClock() : AudioClock(zx::clock(), Source::Invalid, false) {}

  // No copy
  AudioClock(const AudioClock&) = delete;
  AudioClock& operator=(const AudioClock&) = delete;

  // Move is allowed
  AudioClock(AudioClock&& moved_clock) = default;
  AudioClock& operator=(AudioClock&& moved_clock) = default;

  static AudioClock CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain);
  static AudioClock CreateAsDeviceNonadjustable(zx::clock clock, uint32_t domain);
  static AudioClock CreateAsClientAdjustable(zx::clock clock);
  static AudioClock CreateAsClientNonadjustable(zx::clock clock);

  enum class SyncMode {
    // If the client clock is adjustable, we address clock misalignment by rate-adjusting it, even
    // if the device clock is also adjustable. This minimizes disruption to the rest of the system.
    AdjustClientClock,

    // If the device clock is adjustable, AudioCore may choose a client clock for this device clock
    // to follow. If so, AudioCore adjusts the hardware so the device clock aligns with this
    // "hardware-controlling" client clock. If an adjustable device clock has no corresponding
    // client clock that is designated as hardware-controlling, then the device clock is treated as
    // if it were not adjustable.
    AdjustHardwareClock,

    // If neither clock is adjustable, we error-correct by slightly adjusting the sample-rate
    // conversion ratio (referred to as "micro-SRC"). This can occur with an adjustable device
    // clock, if another stream's client clock is already controlling that device clock hardware.
    MicroSrc,

    // If two clocks are identical or in the same clock domain, no synchronization is needed.
    None
  };

  static SyncMode SynchronizationMode(AudioClock& clock1, AudioClock& clock2);

  explicit operator bool() const { return is_valid(); }
  bool is_valid() const { return (source_ != Source::Invalid); }
  bool is_device_clock() const { return (source_ == Source::Device); }
  bool is_client_clock() const { return (source_ == Source::Client); }
  bool is_adjustable() const { return is_adjustable_; }
  bool controls_device_clock() const { return controls_device_clock_; }
  bool set_controls_device_clock(bool controls_device_clock);
  uint32_t domain() const {
    FX_CHECK(is_device_clock());
    return domain_;
  }

  // Return a transform based on a snapshot of the underlying zx::clock
  TimelineFunction ref_clock_to_clock_mono() const;

  // Because 1) AudioClock objects are not copyable, and 2) AudioClock 'consumes' the zx::clock
  // provided to it, and 3) handle values are unique across the system, and 4) even duplicate
  // handles have different values, this all means that the clock handle is essentially the unique
  // ID for this AudioClock object.
  // However, we also want to know whether two unique AudioClocks are based on the same underlying
  // zx::clock (and thus respond identically to rate-adjustment), so we compare the root KOIDs.
  bool operator==(const AudioClock& comparable) const {
    return (audio::clock::GetKoid(clock_) == audio::clock::GetKoid(comparable.clock_));
  }
  // bool operator==(const AudioClock& comparable) const { return (clock_ == comparable.clock_); }
  bool operator!=(const AudioClock& comparable) const { return !(*this == comparable); }

  zx::clock DuplicateClock() const;
  zx::time Read() const;

  zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time) const;
  zx::time MonotonicTimeFromReferenceTime(zx::time ref_time) const;

  // Audio clocks use a PID control so that across a series of external rate adjustments, we
  // smoothly track position (not just rate). The inputs to this feedback loop are destination frame
  // as the "time" or X-axis component, and source position error (in frac frames) as the process
  // variable which we intend to tune to zero. We regularly tune the feedback loop by reporting our
  // current position error at the given time; the PID provides the rate adjustment to be applied.
  //
  // At a given dest_frame, the source position error is provided (in fractional frames). This is
  // used to maintain an adjustment_rate() factor that eliminates the error over time.
  void TuneRateForError(int64_t dest_frame, Fixed frac_src_error);

  // This returns the current rate adjustment factor (a rate close to 1.0) that should be applied,
  // to chase the actual source effectively.
  const TimelineRate& adjustment_rate() const { return adjustment_rate_; }

  // Clear any internal running state (except the PID coefficients themselves), and restart the
  // feedback loop at the given destination frame.
  void ResetRateAdjustment(int64_t dest_frame);

 private:
  enum class Source { Client, Device, Invalid };

  AudioClock(zx::clock clock, Source source, bool is_adjustable, uint32_t domain);
  AudioClock(zx::clock clock, Source source, bool is_adjustable)
      : AudioClock(std::move(clock), source, is_adjustable, kMonotonicDomain) {}

  // This sets the PID coefficients for this clock, depending on the clock type.
  void ConfigureAdjustment(const clock::PidControl::Coefficients& pid_coefficients);

  friend const zx::clock& audio_clock_helper::get_underlying_zx_clock(const AudioClock&);
  zx::clock clock_;

  Source source_;
  bool is_adjustable_;
  uint32_t domain_;  // Will only be used with device clocks.

  // Only used for non-adjustable client clocks.
  bool controls_device_clock_ = false;

  TimelineRate adjustment_rate_;
  audio::clock::PidControl feedback_control_loop_;

  // Only used for adjustable client clocks.
  int32_t adjustment_ppm_ = 0;

  // TODO(fxbug.dev/58541): Refactor to use subclasses for different clock types
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
