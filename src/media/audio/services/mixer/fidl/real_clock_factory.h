// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REAL_CLOCK_FACTORY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REAL_CLOCK_FACTORY_H_

#include <unordered_map>

#include "src/media/audio/services/mixer/fidl/clock_registry.h"

namespace media_audio {

// A factory of RealClocks.
// Not safe for concurrent use.
class RealClockFactory : public ClockFactory {
 public:
  // Implements ClockFactory.
  zx::status<std::pair<std::shared_ptr<Clock>, zx::clock>> CreateGraphControlledClock(
      std::string_view name) override;
  zx::status<std::shared_ptr<Clock>> CreateWrappedClock(zx::clock handle, std::string_view name,
                                                        uint32_t domain, bool adjustable) override;
  std::shared_ptr<Timer> CreateTimer() override;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REAL_CLOCK_FACTORY_H_
