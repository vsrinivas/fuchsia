// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/real_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>

#include <cmath>
#include <string>

namespace media_audio {

// static
std::shared_ptr<RealClock> RealClock::Create(std::string_view name, zx::clock clock,
                                             uint32_t domain, bool adjustable) {
  zx_info_handle_basic_t info;
  auto status = clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "clock.get_info failed, status is " << status;

  // The monotonic domain is not adjustable.
  if (domain == kMonotonicDomain) {
    FX_CHECK(!adjustable) << "the system monotonic clock domain is not adjustable";
  }

  // Adjustable clocks must be writable.
  if (adjustable && (info.rights & ZX_RIGHT_WRITE) == 0) {
    FX_CHECK(false) << "adjustable clock does not have ZX_RIGHT_WRITE, rights are 0x" << std::hex
                    << info.rights;
  }

  // If we can read the clock now, we will always be able to.
  zx_time_t unused;
  status = clock.read(&unused);
  FX_CHECK(status == ZX_OK) << "clock.read failed, status is " << status;

  struct MakePublicCtor : RealClock {
    MakePublicCtor(std::string_view name, zx::clock clock, zx_koid_t koid, uint32_t domain,
                   bool adjustable)
        : RealClock(name, std::move(clock), koid, domain, adjustable) {}
  };

  return std::make_shared<MakePublicCtor>(name, std::move(clock), info.koid, domain, adjustable);
}

// static
std::shared_ptr<RealClock> RealClock::CreateFromMonotonic(std::string_view name, uint32_t domain,
                                                          bool adjustable) {
  zx::clock clock;
  auto status = zx::clock::create(
      ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &clock);
  FX_DCHECK(status == ZX_OK) << "clock.create failed, status is " << status;

  zx_rights_t rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_READ;
  if (adjustable) {
    rights |= ZX_RIGHT_WRITE;
  }
  status = clock.replace(rights, &clock);
  FX_DCHECK(status == ZX_OK) << "clock.replace failed, status is " << status;

  return Create(name, std::move(clock), domain, adjustable);
}

zx::time RealClock::now() const {
  // Create checked that we can call read(), so this should never fail.
  zx::time t;
  auto status = clock_.read(t.get_address());
  FX_CHECK(status == ZX_OK) << "clock.read failed, status is " << status;
  return t;
}

Clock::ToClockMonoSnapshot RealClock::to_clock_mono_snapshot() const {
  // Create checked that we can call read().
  // If we can call read(), we can call get_details(), so this should never fail.
  zx_clock_details_v1_t details;
  auto status = clock_.get_details(&details);
  FX_CHECK(status == ZX_OK) << "clock.get_details failed, status is " << status;

  // get_details gives us mono-to-reference, so invert that to get reference-to-mono.
  return {
      .to_clock_mono = media::TimelineFunction(details.mono_to_synthetic.reference_offset,
                                               details.mono_to_synthetic.synthetic_offset,
                                               details.mono_to_synthetic.rate.reference_ticks,
                                               details.mono_to_synthetic.rate.synthetic_ticks),
      .generation = details.generation_counter,
  };
}

void RealClock::SetRate(int32_t rate_adjust_ppm) {
  FX_CHECK(adjustable()) << "cannot SetRate on unadjustable clocks";

  // Create verified that the clock has ZX_RIGHT_WRITE, so this should never fail.
  zx::clock::update_args args;
  args.reset().set_rate_adjust(rate_adjust_ppm);
  auto status = clock_.update(args);
  FX_CHECK(status == ZX_OK) << "clock.update failed on adjustable clock, status is " << status;
}

std::optional<zx::clock> RealClock::DuplicateZxClockReadOnly() const {
  zx_rights_t rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
  zx::clock dup;
  if (auto status = clock_.duplicate(rights, &dup); status != ZX_OK) {
    FX_LOGS(ERROR) << "RealClock.DuplicateZxClockReadOnly failed with status " << status;
    return std::nullopt;
  }
  return dup;
}

}  // namespace media_audio
