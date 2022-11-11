// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/clock.h>
#include <zircon/types.h>

#include <string>

#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media_audio {

// Abstract base class for clocks in the audio system.
// All methods are safe to call from any thread.
class Clock {
 public:
  static constexpr uint32_t kMonotonicDomain = fuchsia::hardware::audio::CLOCK_DOMAIN_MONOTONIC;
  static constexpr uint32_t kExternalDomain = fuchsia::hardware::audio::CLOCK_DOMAIN_EXTERNAL;

  virtual ~Clock() = default;

  // Reports the clock's name, used for debugging only.
  // Names are not guaranteed to be unique. Use `koid` where a unique identifier is needed.
  virtual std::string_view name() const = 0;

  // Reports the clock's koid.
  // This must uniquely indentify the clock, even if the clock is not backed by a zx::clock.
  virtual zx_koid_t koid() const = 0;

  // Reports the clock's domain. If two clocks have the same clock domain, their clock rates are
  // identical (although their positions may be offset by an arbitrary amount). There are two
  // special values:
  //
  // *  kMonotonicDomain means the hardware is operating at the same rate as the system montonic
  //    clock.
  //
  // *  kExternalDomain means the hardware is operating at an unknown rate and is not synchronized
  //    with any known clock, not even with other clocks in domain kExternalDomain.
  //
  // For clock objects that represent real hardware, the domain typically comes from a system-wide
  // entity such as a global clock tree. For clock objects created in software, the domain is
  // typically either kMonotonicDomain or kExternalDomain.
  virtual uint32_t domain() const = 0;

  // Reports whether this clock can be adjusted via calls to `SetRate`.
  virtual bool adjustable() const = 0;

  // Reports the current time.
  virtual zx::time now() const = 0;

  struct ToClockMonoSnapshot {
    media::TimelineFunction to_clock_mono;
    int64_t generation = -1;
  };

  // Returns a function that translates from this clock's local time, a.k.a. "reference time", to
  // the system monotonic time, along with a generation counter that is incremented each time the
  // `to_clock_mono` function changes.
  virtual ToClockMonoSnapshot to_clock_mono_snapshot() const = 0;

  // Adjusts the clock's rate. The adjustment is given in parts-per-million relate to the system
  // monotonic rate. This parameter has the same constraints as the `rate_adjust` parameter of
  // `zx_clock_update`. Specifically, the rate must be within the range:
  // [ZX_CLOCK_UPDATE_MIN_RATE_ADJUST, ZX_CLOCK_UPDATE_MAX_RATE_ADJUST].
  //
  // It is illegal to call `SetRate` unless the clock is adjustable.
  virtual void SetRate(int32_t rate_adjust_ppm) = 0;

  // Duplicates the underlying zx::clock without ZX_RIGHTS_WRITE, or std::nullopt if there is no
  // underlying zx::clock or it cannot be duplicated.
  //
  // TODO(fxbug.dev/114920): This is needed by old audio_core code only. It's used by FIDL
  // GetReferenceClock implementations which won't be present in the mixer service. Once all uses
  // are removed, this can be deleted.
  virtual std::optional<zx::clock> DuplicateZxClockReadOnly() const = 0;

  //
  // Convenience methods
  //

  // Shorthand for to_clock_mono_snapshot().timeline_function.
  media::TimelineFunction to_clock_mono() const { return to_clock_mono_snapshot().to_clock_mono; }

  // Returns the reference time equivalent to the given system monotonic time.
  zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time) const {
    return zx::time(to_clock_mono().ApplyInverse(mono_time.get()));
  }

  // Returns the system monotonic time equivalent to the given reference time.
  zx::time MonotonicTimeFromReferenceTime(zx::time ref_time) const {
    return zx::time(to_clock_mono().Apply(ref_time.get()));
  }

  // Reports if this clock is currently identical to the system monotonic clock.
  bool IdenticalToMonotonicClock() const {
    auto to_mono = to_clock_mono();
    return to_mono.subject_time() == to_mono.reference_time() &&
           to_mono.subject_delta() == to_mono.reference_delta();
  }

  // Clamps an integer rate, expressed in parts-per-million, to the range allowed by
  // `zx_clock_update`.
  static constexpr int32_t ClampZxClockPpm(int32_t ppm) {
    return std::clamp<int32_t>(ppm, ZX_CLOCK_UPDATE_MIN_RATE_ADJUST,
                               ZX_CLOCK_UPDATE_MAX_RATE_ADJUST);
  }

  // Converts a rational number to parts-per-million, then clamp to range allowed by
  // `zx_clock_update`.
  static constexpr int32_t ClampDoubleToZxClockPpm(double val) {
    return ClampZxClockPpm(static_cast<int32_t>(std::round(static_cast<double>(val) * 1e6)));
  }
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_H_
