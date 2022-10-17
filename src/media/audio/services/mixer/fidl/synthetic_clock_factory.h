// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SYNTHETIC_CLOCK_FACTORY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SYNTHETIC_CLOCK_FACTORY_H_

#include <unordered_map>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/services/mixer/fidl/clock_registry.h"

namespace media_audio {

// A factory of SyntheticClocks.
// Not safe for concurrent use.
class SyntheticClockFactory : public ClockFactory {
 public:
  explicit SyntheticClockFactory(std::shared_ptr<SyntheticClockRealm> realm)
      : realm_(std::move(realm)),
        system_mono_(realm_->CreateClock("SystemMonotonicClock", Clock::kMonotonicDomain,
                                         /*adjustable=*/false)) {}

  // Implements ClockFactory.
  std::shared_ptr<const Clock> SystemMonotonicClock() const override { return system_mono_; }
  zx::result<std::pair<std::shared_ptr<Clock>, zx::clock>> CreateGraphControlledClock(
      std::string_view name) override;
  zx::result<std::shared_ptr<Clock>> CreateWrappedClock(zx::clock handle, std::string_view name,
                                                        uint32_t domain, bool adjustable) override;
  std::shared_ptr<Timer> CreateTimer() override;

 private:
  const std::shared_ptr<SyntheticClockRealm> realm_;
  const std::shared_ptr<const Clock> system_mono_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SYNTHETIC_CLOCK_FACTORY_H_
