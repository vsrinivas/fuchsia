// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_FAKE_AUDIO_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_FAKE_AUDIO_CLOCK_H_

#include "src/media/audio/lib/clock/testing/fake_audio_clock_factory.h"

namespace media::audio::testing {

class FakeAudioClock : public AudioClock {
 public:
  static FakeAudioClock ClientAdjustable(std::shared_ptr<FakeAudioClockFactory> factory,
                                         zx::clock clock) {
    return FakeAudioClock(std::move(factory), std::move(clock), Source::Client, true);
  }

  static FakeAudioClock ClientFixed(std::shared_ptr<FakeAudioClockFactory> factory,
                                    zx::clock clock) {
    return FakeAudioClock(std::move(factory), std::move(clock), Source::Client, false);
  }

  static FakeAudioClock DeviceAdjustable(std::shared_ptr<FakeAudioClockFactory> factory,
                                         zx::clock clock, uint32_t domain) {
    return FakeAudioClock(std::move(factory), std::move(clock), Source::Device, true, domain);
  }

  static FakeAudioClock DeviceFixed(std::shared_ptr<FakeAudioClockFactory> factory, zx::clock clock,
                                    uint32_t domain) {
    return FakeAudioClock(std::move(factory), std::move(clock), Source::Device, false, domain);
  }

  TimelineFunction ref_clock_to_clock_mono() const override {
    return factory_->ref_to_mono_time_transform(clock_id_);
  }

  zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time) const override {
    return zx::time{factory_->ref_to_mono_time_transform(clock_id_).ApplyInverse(mono_time.get())};
  }

  zx::time MonotonicTimeFromReferenceTime(zx::time ref_time) const override {
    return zx::time{factory_->ref_to_mono_time_transform(clock_id_).Apply(ref_time.get())};
  }

  zx::time Read() const override { return ReferenceTimeFromMonotonicTime(factory_->mono_time()); }

  void UpdateClockRate(int32_t rate_adjust_ppm) override {
    // Simulate zx_clock_update, which ignores out-of-range rates.
    if (rate_adjust_ppm < ZX_CLOCK_UPDATE_MIN_RATE_ADJUST ||
        rate_adjust_ppm > ZX_CLOCK_UPDATE_MAX_RATE_ADJUST) {
      FX_LOGS(WARNING) << "FakeAudioClock::UpdateClockRate rate_adjust_ppm out of bounds";
      return;
    }
    factory_->UpdateClockRate(clock_id_, rate_adjust_ppm);
  }

 private:
  FakeAudioClock(std::shared_ptr<FakeAudioClockFactory> factory, zx::clock clock, Source source,
                 bool adjustable, uint32_t domain = kInvalidDomain)
      : AudioClock(std::move(clock), source, adjustable, domain),
        factory_(std::move(factory)),
        clock_id_(audio::clock::GetKoid(DuplicateClock().take_value())) {}

  std::shared_ptr<FakeAudioClockFactory> factory_;
  zx_koid_t clock_id_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_FAKE_AUDIO_CLOCK_H_
