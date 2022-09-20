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

  // Returns a singleton which represents the system monotonic clock.
  virtual std::shared_ptr<const Clock> SystemMonotonicClock() const = 0;

  // Creates a graph-controlled clock with the given. The return value includes an actual Clock
  // object along with a zx::clock handle which must have the same koid as the Clock. The returned
  // Clock mjust be adjustable. The returned handle must have ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER
  // and must not have ZX_RIGHT_WRITE.
  //
  // Errors:
  // * Anything returned by zx_clock_create.
  virtual zx::status<std::pair<std::shared_ptr<Clock>, zx::clock>> CreateGraphControlledClock(
      std::string_view name) = 0;

  // Creates a clock which wraps the given zx::clock handle.
  //
  // Errors:
  // * Anything returned by zx_clock_create.
  // * ZX_ERR_NOT_SUPPORTED if the factory doesn't support wrapping zx::clock handles.
  virtual zx::status<std::shared_ptr<Clock>> CreateWrappedClock(zx::clock handle,
                                                                std::string_view name,
                                                                uint32_t domain,
                                                                bool adjustable) = 0;

  // Creates a user-controlled clock with the given properties.
  virtual std::shared_ptr<Timer> CreateTimer() = 0;
};

// Contains the set of all clocks used by a single mix graph. Each ClockRegistry is backed by a
// single ClockFactory. All clocks contained in this registry are guaranteed to have unique koids.
// Given two `std::shared_ptr<Clock>` pointers `c1` and `c2`:
//
// ```
// c1->koid() == c2->koid() iff c1.get() == c2.get()
// ```
//
// Not safe for concurrent use.
class ClockRegistry {
 public:
  explicit ClockRegistry(std::shared_ptr<ClockFactory> factory) : factory_(std::move(factory)) {}

  // Returns a singleton which represents the system monotonic clock.
  std::shared_ptr<const Clock> SystemMonotonicClock() const;

  // Creates a graph-controlled clock. The return value includes an actual Clock object along with a
  // zx::clock handle which can identify the Clock in future FindClock calls. The returned Clock is
  // adjustable. The returned handle is guaranteed to have ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER.
  //
  // The error, if any, comes from the underlying ClockFactory.
  zx::status<std::pair<std::shared_ptr<Clock>, zx::clock>> CreateGraphControlledClock();

  // Creates a user-controlled clock. The returned Clock wraps `handle` and is not adjustable.
  // The error, if any, comes from the underlying ClockFactory.
  zx::status<std::shared_ptr<Clock>> CreateUserControlledClock(zx::clock handle,
                                                               std::string_view name,
                                                               uint32_t domain);

  // Adds the given Clock. This is useful for clocks that were created via an out-of-band mechanism.
  // The above Create methods call AddClock automatically.
  //
  // Errors:
  // * ZX_ERR_ALREADY_EXISTS if a clock with the same koid already exists.
  //
  // TODO(fxbug.dev/87651): need to allow clocks shared by multiple nodes
  zx::status<> AddClock(std::shared_ptr<Clock> clock);

  // Looks up the Clock with the same koid as `handle`.
  //
  // Errors:
  // * ZX_ERR_BAD_HANDLE if the handle is invalid.
  // * ZX_ERR_NOT_FOUND if a clock with the same koid does not exist.
  zx::status<std::shared_ptr<Clock>> FindClock(const zx::clock& handle);

  // Forgets a clock which was previously added.
  //
  // Errors:
  // * ZX_ERR_BAD_HANDLE if the handle is invalid.
  //
  // TODO(fxbug.dev/87651): need to allow clocks shared by multiple nodes
  zx::status<> ForgetClock(const zx::clock& handle);

  // Uses the underlying factory to create a timer.
  std::shared_ptr<Timer> CreateTimer();

  // TODO(fxbug.dev/87651): also add
  // CreateSynchronizer(source_clock, dest_clock)
  // RemoveSynchronizer(source_clock, dest_clock)

 private:
  const std::shared_ptr<ClockFactory> factory_;

  std::unordered_map<zx_koid_t, std::shared_ptr<Clock>> clocks_;
  int64_t num_graph_controlled_ = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CLOCK_REGISTRY_H_
