// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/synthetic_clock_factory.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

zx::result<std::pair<std::shared_ptr<Clock>, zx::clock>>
SyntheticClockFactory::CreateGraphControlledClock(std::string_view name) {
  auto clock = realm_->CreateClock(name, Clock::kExternalDomain, /*adjustable=*/true);
  auto handle = clock->DuplicateZxClockUnreadable();
  return zx::ok(std::make_pair(std::move(clock), std::move(handle)));
}

zx::result<std::shared_ptr<Clock>> SyntheticClockFactory::CreateWrappedClock(zx::clock handle,
                                                                             std::string_view name,
                                                                             uint32_t domain,
                                                                             bool adjustable) {
  // SyntheticClocks cannot be created from a handle. They must be created from a realm.
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

std::shared_ptr<Timer> SyntheticClockFactory::CreateTimer() { return realm_->CreateTimer(); }

}  // namespace media_audio
