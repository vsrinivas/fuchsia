// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_REAL_CLOCK_REGISTRY_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_REAL_CLOCK_REGISTRY_H_

#include <unordered_map>

#include "src/media/audio/mixer_service/fidl/clock_registry.h"

namespace media_audio {

// A registry of RealClocks.
// Not safe for concurrent use.
class RealClockRegistry : public ClockRegistry {
 public:
  zx::clock CreateGraphControlled() override;
  std::shared_ptr<Clock> FindOrCreate(zx::clock zx_clock, std::string_view name,
                                      uint32_t domain) override;

 private:
  std::unordered_map<zx_koid_t, std::shared_ptr<RealClock>> clocks_;
  uint64_t num_graph_controlled_ = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_REAL_CLOCK_REGISTRY_H_
