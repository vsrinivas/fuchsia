// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/real_clock_registry.h"

#include <lib/syslog/cpp/macros.h>

namespace media_audio_mixer_service {

zx::clock RealClockRegistry::CreateGraphControlled() {
  // These system calls shouldn't fail, unless our parameters are invalid, which should not happen.
  zx::clock adjustable_clock;
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                        nullptr, &adjustable_clock);
  FX_CHECK(status == ZX_OK) << "zx::clock::create failed with status " << status;

  // We return an unadjustable duplicate.
  zx::clock unadjustable_clock;
  status = adjustable_clock.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ,
                                      &unadjustable_clock);
  FX_CHECK(status == ZX_OK) << "zx::clock::duplicate failed with status " << status;

  // This cannot fail (`adjustable_clock` must be a valid clock handle).
  const auto koid_result = ZxClockToKoid(adjustable_clock);
  FX_CHECK(koid_result.is_ok());
  const auto koid = koid_result.value();

  // The RealClock uses the adjustable clock.
  clocks_[koid] = RealClock::Create(
      std::string("GraphControlled") + std::to_string(num_graph_controlled_),
      std::move(adjustable_clock), Clock::kExternalDomain, /* adjustable = */ true);
  num_graph_controlled_++;

  return unadjustable_clock;
}

std::shared_ptr<Clock> RealClockRegistry::FindOrCreate(zx::clock zx_clock, std::string_view name,
                                                       uint32_t domain) {
  const auto koid_result = ZxClockToKoid(zx_clock);
  if (!koid_result.is_ok()) {
    return nullptr;
  }

  const auto koid = koid_result.value();
  if (auto it = clocks_.find(koid); it != clocks_.end()) {
    return it->second;
  }

  // The clock is not adjustable.
  auto clock = RealClock::Create(name, std::move(zx_clock), domain, /* adjustable = */ false);
  clocks_[koid] = clock;
  return clock;
}

}  // namespace media_audio_mixer_service
