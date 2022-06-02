// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_CLOCK_REGISTRY_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_CLOCK_REGISTRY_H_

#include <lib/zx/status.h>

#include "src/media/audio/mixer_service/common/basic_types.h"

namespace media_audio_mixer_service {

// An abstract registry of all clocks used by a mix graph.
// Not safe for concurrent use.
class ClockRegistry {
 public:
  virtual ~ClockRegistry() = default;

  // Creates a graph-controlled clock. The returned clock has (at least) ZX_RIGHT_DUPLICATE |
  // ZX_RIGHT_TRANSFER.
  virtual zx::clock CreateGraphControlled() = 0;

  // Looks up a clock, or if it does not yet exist, create a new unadustable Clock using the given
  // zx::clock, name, and domain. Each unique zx::clock (identified by koid) is associated with a
  // unique Clock object, meaning `c1->koid() == c2->koid()` iff `c1.get() == c2.get()`.
  //
  // Returns nullptr if the clock is not found and cannot be created.
  virtual std::shared_ptr<Clock> FindOrCreate(zx::clock zx_clock, std::string_view name,
                                              uint32_t domain) = 0;

  // Returns the koid of `clock`, or an error on failure.
  static zx::status<zx_koid_t> ZxClockToKoid(const zx::clock& clock);
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_FIDL_CLOCK_REGISTRY_H_
