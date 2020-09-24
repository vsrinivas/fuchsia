// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/sound.h"

namespace soundplayer {
namespace {

static constexpr uint64_t kNanosPerSecond = 1000000000;

}  // namespace

Sound::Sound(zx::vmo vmo, uint64_t size, fuchsia::media::AudioStreamType stream_type)
    : vmo_(std::move(vmo)), size_(size), stream_type_(std::move(stream_type)) {}

Sound::Sound() : size_(0) {}

Sound::Sound(Sound&& other) noexcept
    : vmo_(std::move(other.vmo_)),
      size_(other.size_),
      stream_type_(std::move(other.stream_type_)) {}

Sound::~Sound() {}

Sound& Sound::operator=(Sound&& other) noexcept {
  vmo_ = std::move(other.vmo_);
  size_ = other.size_;
  stream_type_ = std::move(other.stream_type_);
  return *this;
}

zx::duration Sound::duration() const {
  return zx::nsec(kNanosPerSecond * size_) / stream_type_.channels / sizeof(int16_t) /
         stream_type_.frames_per_second;
}

uint64_t Sound::frame_count() const { return size_ / frame_size(); }

uint32_t Sound::frame_size() const { return sample_size() * stream_type_.channels; }

uint32_t Sound::sample_size() const {
  switch (stream_type_.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return sizeof(uint8_t);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return sizeof(int16_t);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return sizeof(int32_t);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return sizeof(float);
  }
}

}  // namespace soundplayer
