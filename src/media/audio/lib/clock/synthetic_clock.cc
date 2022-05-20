// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/synthetic_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <cmath>
#include <string>

namespace media_audio {

// static
std::shared_ptr<SyntheticClock> SyntheticClock::Create(
    std::string_view name, uint32_t domain, bool adjustable,
    std::shared_ptr<const SyntheticClockRealm> realm, media::TimelineFunction to_clock_mono) {
  // The monotonic domain is not adjustable.
  if (domain == kMonotonicDomain) {
    FX_CHECK(!adjustable) << "the system monotonic clock domain is not adjustable";
  }

  // Since every clock needs a koid, create a zx::clock so we have a koid.
  zx::clock clock;
  auto status = zx::clock::create(0, nullptr, &clock);
  FX_CHECK(status == ZX_OK) << "clock.create failed, status is " << status;

  zx_info_handle_basic_t info;
  status = clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "clock.get_info failed, status is " << status;

  struct MakePublicCtor : SyntheticClock {
    MakePublicCtor(std::string_view name, zx::clock clock, zx_koid_t koid, uint32_t domain,
                   bool adjustable, std::shared_ptr<const SyntheticClockRealm> realm,
                   media::TimelineFunction to_clock_mono)
        : SyntheticClock(name, std::move(clock), koid, domain, adjustable, std::move(realm),
                         to_clock_mono) {}
  };

  return std::make_shared<MakePublicCtor>(name, std::move(clock), info.koid, domain, adjustable,
                                          std::move(realm), to_clock_mono);
}

zx::time SyntheticClock::now() const {
  auto mono_now = realm_->now();

  std::lock_guard<std::mutex> lock(mutex_);
  return MonoToRef(to_clock_mono_, mono_now);
}

Clock::ToClockMonoSnapshot SyntheticClock::to_clock_mono_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {
      .to_clock_mono = to_clock_mono_,
      .generation = generation_,
  };
}

void SyntheticClock::SetRate(int32_t rate_adjust_ppm) {
  FX_CHECK(adjustable()) << "cannot SetRate on unadjustable clocks";

  // Just like zx_clock_update, fail if the rate is out-of-range.
  FX_CHECK(rate_adjust_ppm >= ZX_CLOCK_UPDATE_MIN_RATE_ADJUST &&
           rate_adjust_ppm <= ZX_CLOCK_UPDATE_MAX_RATE_ADJUST)
      << "SetRate(" << rate_adjust_ppm << ") is outside the legal range ["
      << ZX_CLOCK_UPDATE_MIN_RATE_ADJUST << ", " << ZX_CLOCK_UPDATE_MAX_RATE_ADJUST << "]";

  auto mono_now = realm_->now();

  std::lock_guard<std::mutex> lock(mutex_);
  auto ref_now = MonoToRef(to_clock_mono_, mono_now);
  to_clock_mono_ = media::TimelineFunction(mono_now.get(), ref_now.get(), 1'000'000,
                                           1'000'000 + rate_adjust_ppm);
  generation_++;
}

std::optional<zx::clock> SyntheticClock::DuplicateZxClockReadOnly() const {
  FX_LOGS(ERROR) << "SyntheticClock does not have a readable zx::clock to duplicate";
  return std::nullopt;
}

// static
std::shared_ptr<SyntheticClockRealm> SyntheticClockRealm::Create() {
  struct MakePublicCtor : SyntheticClockRealm {
    MakePublicCtor() : SyntheticClockRealm() {}
  };
  return std::make_shared<MakePublicCtor>();
}

// static
std::shared_ptr<SyntheticClock> SyntheticClockRealm::CreateClock(
    std::string_view name, uint32_t domain, bool adjustable,
    media::TimelineFunction to_clock_mono) {
  return SyntheticClock::Create(name, domain, adjustable, shared_from_this(), to_clock_mono);
}

zx::time SyntheticClockRealm::now() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mono_now_;
}

void SyntheticClockRealm::AdvanceTo(zx::time mono_now) {
  std::lock_guard<std::mutex> lock(mutex_);
  AdvanceToImpl(mono_now);
}

void SyntheticClockRealm::AdvanceBy(zx::duration mono_diff) {
  std::lock_guard<std::mutex> lock(mutex_);
  AdvanceToImpl(mono_now_ + mono_diff);
}

void SyntheticClockRealm::AdvanceToImpl(zx::time mono_now) {
  FX_CHECK(mono_now > mono_now_);
  mono_now_ = mono_now;
}

}  // namespace media_audio
