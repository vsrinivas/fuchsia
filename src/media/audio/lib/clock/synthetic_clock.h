// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_CLOCK_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "src/media/audio/lib/clock/clock.h"

namespace media_audio {

class SyntheticClockRealm;

// An implementation of Clock that is controlled by a SyntheticClockRealm. To create a
// SyntheticClock, see SyntheticClockRealm::CreateClock.
//
// All methods are safe to call from any thread.
class SyntheticClock : public Clock {
 public:
  std::string_view name() const override { return name_; }
  zx_koid_t koid() const override { return koid_; }
  uint32_t domain() const override { return domain_; }
  bool adjustable() const override { return adjustable_; }

  zx::time now() const override;
  ToClockMonoSnapshot to_clock_mono_snapshot() const override;
  void SetRate(int32_t rate_adjust_ppm) override;
  std::optional<zx::clock> DuplicateZxClockReadOnly() const override;

  // Duplicates the underlying zx::clock with ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER but not
  // ZX_RIGHT_READ or ZX_RIGHT_WRITE. The returned zx::clock can act as a handle for this
  // SyntheticClock since its koid matches `koid()`. However, the zx::clock is not readable because
  // its value is not synchronized with this SyntheticClock.
  zx::clock DuplicateZxClockUnreadable() const;

 private:
  friend class SyntheticClockRealm;

  static zx::time MonoToRef(const media::TimelineFunction& to_clock_mono, zx::time mono_time) {
    return zx::time(to_clock_mono.ApplyInverse(mono_time.get()));
  }

  static std::shared_ptr<SyntheticClock> Create(std::string_view name, uint32_t domain,
                                                bool adjustable,
                                                std::shared_ptr<const SyntheticClockRealm> realm,
                                                media::TimelineFunction to_clock_mono);

  SyntheticClock(std::string_view name, zx::clock clock, zx_koid_t koid, uint32_t domain,
                 bool adjustable, std::shared_ptr<const SyntheticClockRealm> realm,
                 media::TimelineFunction to_clock_mono)
      : name_(name),
        zx_clock_(std::move(clock)),
        koid_(koid),
        domain_(domain),
        adjustable_(adjustable),
        realm_(std::move(realm)),
        to_clock_mono_(to_clock_mono) {}

  const std::string name_;
  const zx::clock zx_clock_;
  const zx_koid_t koid_;
  const uint32_t domain_;
  const bool adjustable_;
  const std::shared_ptr<const SyntheticClockRealm> realm_;

  mutable std::mutex mutex_;
  media::TimelineFunction to_clock_mono_ TA_GUARDED(mutex_);
  int64_t generation_ TA_GUARDED(mutex_) = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_CLOCK_H_
