// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_REAL_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_REAL_CLOCK_H_

#include <lib/zx/clock.h>

#include <memory>
#include <string>

#include "src/media/audio/lib/clock/clock.h"

namespace media_audio {

// A clock that is backed by a zx::clock.
// All methods are safe to call from any thread.
class RealClock : public Clock {
 public:
  // Creates a clock from a zx::clock.
  // The `clock` must be started and must be ZX_CLOCK_OPT_CONTINUOUS and ZX_CLOCK_OPT_MONOTONIC.
  // If `adjustable`, the `clock` must have ZX_RIGHT_WRITE.
  [[nodiscard]] static std::shared_ptr<RealClock> Create(std::string_view name, zx::clock clock,
                                                         uint32_t domain, bool adjustable);

  // Creates a clock which is initially identical to the system monotonic clock.
  // If `adjustable`, the clock can be adjusted.
  // If `!adjustable`, the clock will always have `IdenticalToMonotonicClock() == true`.
  // If called multiple times, this will create distinct clocks with different koids.
  static std::shared_ptr<RealClock> CreateFromMonotonic(std::string_view name, uint32_t domain,
                                                        bool adjustable);

  std::string_view name() const override { return name_; }
  zx_koid_t koid() const override { return koid_; }
  uint32_t domain() const override { return domain_; }
  bool adjustable() const override { return adjustable_; }

  zx::time now() const override;
  ToClockMonoSnapshot to_clock_mono_snapshot() const override;
  void SetRate(int32_t rate_adjust_ppm) override;
  std::optional<zx::clock> DuplicateZxClockReadOnly() const override;

 private:
  RealClock(std::string_view name, zx::clock clock, zx_koid_t koid, uint32_t domain,
            bool adjustable)
      : name_(name),
        clock_(std::move(clock)),
        koid_(koid),
        domain_(domain),
        adjustable_(adjustable) {}

  const std::string name_;
  const zx::clock clock_;
  const zx_koid_t koid_;
  const uint32_t domain_;
  const bool adjustable_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_REAL_CLOCK_H_
