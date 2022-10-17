// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/real_clock_factory.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/lib/clock/real_timer.h"
#include "src/media/audio/services/common/logging.h"

namespace media_audio {

namespace {
std::shared_ptr<const Clock> CreateSystemMonotonicClock() {
  zx::clock mono;
  auto status = zx::clock::create(
      ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &mono);
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::clock::create failed for system monotonic clock";
  }
  return RealClock::Create("SystemMonotonicClock", std::move(mono), Clock::kMonotonicDomain,
                           /*adjustable=*/false);
}
}  // namespace

RealClockFactory::RealClockFactory() : system_mono_(CreateSystemMonotonicClock()) {}

zx::result<std::pair<std::shared_ptr<Clock>, zx::clock>>
RealClockFactory::CreateGraphControlledClock(std::string_view name) {
  // Create a new zx::clock.
  zx::clock adjustable_handle;
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                        nullptr, &adjustable_handle);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // We return an unadjustable duplicate.
  zx::clock unadjustable_handle;
  status = adjustable_handle.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ,
                                       &unadjustable_handle);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto clock = RealClock::Create(name, std::move(adjustable_handle), Clock::kExternalDomain,
                                 /*adjustable=*/true);
  return zx::ok(std::make_pair(std::move(clock), std::move(unadjustable_handle)));
}

zx::result<std::shared_ptr<Clock>> RealClockFactory::CreateWrappedClock(zx::clock handle,
                                                                        std::string_view name,
                                                                        uint32_t domain,
                                                                        bool adjustable) {
  return zx::ok(RealClock::Create(name, std::move(handle), domain, adjustable));
}

std::shared_ptr<Timer> RealClockFactory::CreateTimer() { return RealTimer::Create({}); }

}  // namespace media_audio
