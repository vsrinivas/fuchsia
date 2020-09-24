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
  // Clock sources
  // Clocks are created and maintained by clients, or by audio_core on behalf of an audio device.
  // This is encoded in the clock's Source.
  //
  // Clock types
  // A clock's Type
  // specifies whether the clock can be rate-adjusted by AudioCore. It encodes the intention of the
  // client or the capabilities of the hardware.
  //
  // Client's choose whether to use the audio_core-supplied Optimal clock, or to supply their own
  // Custom clock. From AudioCore's standpoint, the Optimal clock is Adjustable (we provide it to
  // the client as read-only; we can adjust it as necessary). Any clock supplied by the client is
  // labelled Custom and treated (from AudioCore's standpoint) as read-only and Non-Adjustable (the
  // client can rate-adjust it, but that is outside our view). AudioCore might mark a Custom clock
  // as Hardware-Controlling (there will be at most one of these for each Adjustable Device clock),
  // but this is outside the client's control or visibility.
  //
  // Device clocks might be Adjustable as well, depending on hardware design and driver support.
  // This represents the ability of actual clock hardware to be fine-tuned via software. The
  // underlying zx::clock object is maintained by AudioCore, based on position notifications
  // from the audio driver that relate audio hardware DMA position to local monotonic clock time.
  //
  // Clock synchronization
  // When a client clock and a device clock run at slightly different rates, we error-correct in
  // order to keep them synchronized. Exactly how we do so depends on their respective types.
  //
  // If the client clock is adjustable, we reconcile clock misalignment by rate-adjusting it (even
  // if the device clock is also adjustable). This minimizes disruption to the rest of the system.
  //
  // If the device clock is adjustable, AudioCore might designate the (non-adjustable) client clock
  // as hardware-controlling, in which case we rate-adjust the device clock hardware to align the
  // device clock with the client clock. At most, only one client clock can be marked as controlling
  // that device clock. If the client clock is NOT hardware-controlling, it cannot guide our
  // adjustment of the device clock; we treat this case as if neither clock is adjustable.
  //
  // If neither clock is adjustable, we error-correct by slightly adjusting our sample-rate
  // conversion ratio (referred to as "micro-SRC"). This can occur if hardware rate-adjustment is
  // not supported by hardware, audio driver and AudioCore; it can also occur if another client
  // clock is already controlling that device clock hardware.
  //
  // Clock domains
  // A clock domain groups a set of clocks that always progress at the same rate (although they may
  // have offsets). Adjusting one clock causes all others in that same domain to respond as one.

  // Adjustable device clocks, by definition, are NOT rate-locked to the local monotonic clock and
  // must always be in a separate domain. However clock domain is distinct from adjustability.
  // A non-adjustable clock might also be in a different domain from the local monotonic clock
  // (CLOCK_DOMAIN_MONOTONIC, defined in fuchsia.hardware.audio/stream.fidl), in which case it may
  // drift relative to the system clock; all clocks in that domain would drift as one.
  //
  // Clock drift
  // AudioCore handles hardware clock drift just as it would any other clock synchronization.
  // It passes any error onward to the client clock if it can; otherwise it adjusts clock hardware
  // if possible; else it inserts software-based "micro-SRC".
  //
  // Feedback control
  // One final note: when a client adjusts the rate of a clock, or even when we adjust the clock
  // hardware's rate, we do not know the exact instant of that rate change. Our rate adjustments
  // might overshoot or undershoot our intention; thus we must track POSITION (not just rate), and
  // eliminate any error over time with a feedback control loop.

  // Results
  // Based on current PID coefficients (thus may need adjusting as we tune coefficients), our
  // worst-case position error deviation for each ppm of rate change is ~16 frac frames on the
  // "major" (immediate response) side, and ~8 frac frames on the "minor" (correct for overshoot)
  // side. This equates to about 40nsec per ppm and 20nsec per ppm at 48khz, so a sudden change of
  // 1000ppm in clock rate would cause a worst-case desync position error of about 16000 frac
  // frames, or about 2 frames, or about 40 usec.
  //
  // For very low-magnitude rate adjustment (1-2 ppm), we are slightly worse than linear and might
  // experience desync of up to +/-40nsec (16 fractional frames).
  //
  // Once we've settled back to steady state, our desync ripple is +/-20nsec (8 fractional frames).

  static constexpr uint32_t kMonotonicDomain = fuchsia::hardware::audio::CLOCK_DOMAIN_MONOTONIC;

  friend const zx::clock& audio_clock_helper::get_underlying_zx_clock(const AudioClock&);

  AudioClock() : AudioClock(zx::clock(), Source::Client, Type::Invalid) {}

  // No copy
  AudioClock(const AudioClock&) = delete;
  AudioClock& operator=(const AudioClock&) = delete;

  // Move is allowed
  AudioClock(AudioClock&& moved_clock) = default;
  AudioClock& operator=(AudioClock&& moved_clock) = default;

  static AudioClock CreateAsDeviceAdjustable(zx::clock clock, uint32_t domain);
  static AudioClock CreateAsDeviceStatic(zx::clock clock, uint32_t domain);
  static AudioClock CreateAsOptimal(zx::clock clock);
  static AudioClock CreateAsCustom(zx::clock clock);
  bool SetAsHardwareControlling(bool controls_hw_clock);

  explicit operator bool() const { return is_valid(); }
  bool is_valid() const { return (type_ != Type::Invalid); }
  bool is_adjustable() const { return (type_ == Type::Adjustable); }
  bool is_device_clock() const { return (source_ == Source::Device); }
  bool is_client_clock() const { return !is_device_clock(); }
  bool controls_hardware_clock() const { return controls_hardware_clock_; }
  uint32_t domain() const {
    FX_CHECK(is_device_clock());
    return domain_;
  }

  const TimelineFunction& ref_clock_to_clock_mono();
  const TimelineFunction& quick_ref_clock_to_clock_mono() const { return ref_clock_to_clock_mono_; }

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
  // used to maintain a rate_adjustment() factor that eliminates the error over time.
  void TuneRateForError(int64_t dest_frame, Fixed frac_src_error);

  // This returns the current rate adjustment factor (a rate close to 1.0) that should be applied,
  // to chase the actual source in an optimal manner.
  const TimelineRate& rate_adjustment() const { return rate_adjustment_; }

  // Clear any internal running state (except the PID coefficients themselves), and restart the
  // feedback loop at the given destination frame.
  void ResetRateAdjustment(int64_t dest_frame);

 private:
  enum class Source { Device, Client };
  enum class Type { Adjustable, NonAdjustable, Invalid };

  AudioClock(zx::clock clock, Source source, Type type, uint32_t domain);
  AudioClock(zx::clock clock, Source source, Type type)
      : AudioClock(std::move(clock), source, type, kMonotonicDomain) {}

  // This sets the PID coefficients for this clock, depending on the clock type.
  void ConfigureAdjustment(const clock::PidControl::Coefficients& pid_coefficients);

  zx::clock clock_;
  Source source_;
  Type type_;
  uint32_t domain_;  // Only really used for device clocks.

  // Only used for non-adjustable client clocks. Consider moving to a subclass structure
  bool controls_hardware_clock_ = false;

  TimelineFunction ref_clock_to_clock_mono_;

  TimelineRate rate_adjustment_;
  audio::clock::PidControl rate_adjuster_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
