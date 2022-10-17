// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CLOCK_REGISTRY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CLOCK_REGISTRY_H_

#include <lib/zx/status.h>

#include <unordered_map>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/timer.h"
#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {

// An abstract factory for creating clocks and timers. All clocks and timers created by this factory
// are members of the same clock "realm", meaning they follow a shared system monotonic clock. In
// practice, implementions use either the "real" realm, which follows the real system monotonic
// clock, or a SyntheticClockRealm.
//
// Implementations are not safe for concurrent use.
class ClockFactory {
 public:
  virtual ~ClockFactory() = default;

  // Returns a singleton which represents the system monotonic clock for this realm.
  virtual std::shared_ptr<const Clock> SystemMonotonicClock() const = 0;

  // Creates a graph-controlled clock with the given. The return value includes an actual Clock
  // object along with a zx::clock handle which must have the same koid as the Clock. The returned
  // Clock mjust be adjustable. The returned handle must have ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER
  // and must not have ZX_RIGHT_WRITE.
  //
  // Errors:
  // * Anything returned by zx_clock_create.
  virtual zx::result<std::pair<std::shared_ptr<Clock>, zx::clock>> CreateGraphControlledClock(
      std::string_view name) = 0;

  // Creates a clock which wraps the given zx::clock handle.
  //
  // Errors:
  // * Anything returned by zx_clock_create.
  // * ZX_ERR_NOT_SUPPORTED if the factory doesn't support wrapping zx::clock handles.
  virtual zx::result<std::shared_ptr<Clock>> CreateWrappedClock(zx::clock handle,
                                                                std::string_view name,
                                                                uint32_t domain,
                                                                bool adjustable) = 0;

  // Creates a user-controlled clock with the given properties.
  virtual std::shared_ptr<Timer> CreateTimer() = 0;
};

// Contains the set of all clocks used by a single mix graph. All clocks contained in this registry
// are guaranteed to have unique koids. Given two `std::shared_ptr<Clock>` pointers `c1` and `c2`:
//
// ```
// c1->koid() == c2->koid() iff c1.get() == c2.get()
// ```
//
// After `Add(clock)`, the registry creates a mapping from `clock->koid()` to `weak_ptr(clock)`.
// This mapping will live until all other references to `clock` are dropped, at which point the
// mapping will be garbage collected.
//
// Not safe for concurrent use.
class ClockRegistry {
 public:
  // Adds the given clock.
  //
  // REQUIRED: There must not by any registered clocks with the same koid as `clock`.
  void Add(std::shared_ptr<Clock> clock);

  // Looks up the clock with the given koid.
  //
  // Errors:
  // * ZX_ERR_NOT_FOUND if a clock with the same koid does not exist.
  zx::result<std::shared_ptr<Clock>> Find(zx_koid_t koid);

  // Looks up the clock with the same koid as `handle`.
  //
  // Errors:
  // * ZX_ERR_BAD_HANDLE if the handle is invalid.
  // * ZX_ERR_NOT_FOUND if a clock with the same koid does not exist.
  zx::result<std::shared_ptr<Clock>> Find(const zx::clock& handle);

  // TODO(fxbug.dev/87651): also add
  // CreateSynchronizer(source_clock, dest_clock)
  // RemoveSynchronizer(source_clock, dest_clock)

 private:
  std::unordered_map<zx_koid_t, std::weak_ptr<Clock>> clocks_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CLOCK_REGISTRY_H_
