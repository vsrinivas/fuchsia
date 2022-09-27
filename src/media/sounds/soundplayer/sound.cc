// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/sound.h"

namespace soundplayer {
namespace {

static constexpr uint64_t kNanosPerSecond = 1000000000;

bool operator==(const fuchsia::media::AudioStreamType& lhs,
                const fuchsia::media::AudioStreamType& rhs) {
  return lhs.sample_format == rhs.sample_format && lhs.channels == rhs.channels &&
         lhs.frames_per_second == rhs.frames_per_second;
}

}  // namespace

Sound::Sound(zx::vmo vmo, uint64_t size, fuchsia::media::AudioStreamType stream_type)
    : vmo_(std::move(vmo)), size_(size), stream_type_(std::move(stream_type)) {
  FX_CHECK(vmo_.get_size(&vmo_size_) == ZX_OK);
}

const zx::vmo& Sound::LockForRead() {
  FX_DCHECK(lock_count_ >= 0);

  if (++lock_count_ == 1) {
    ApplyLockForRead();
  }

  return vmo();
}

const zx::vmo& Sound::LockForWrite() {
  FX_DCHECK(lock_count_ >= 0);

  if (++lock_count_ == 1) {
    ApplyLockForWrite();
  }

  return vmo();
}

void Sound::Unlock() {
  FX_DCHECK(lock_count_ > 0);

  if (--lock_count_ == 0) {
    Removelock();
  }
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

///////////////////////////////////////////////////////////////////////////////////////////////////
// DiscardableSound definitions.

void DiscardableSound::ApplyLockForRead() {
  FX_DCHECK(vmo());

  zx_vmo_lock_state lock_state;
  zx_status_t status =
      vmo().op_range(ZX_VMO_OP_LOCK, 0, vmo_size(), &lock_state, sizeof(lock_state));
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to lock vmo for read";
  }

  if (lock_state.discarded_size > 0) {
    Restore();
  }
}

void DiscardableSound::ApplyLockForWrite() {
  FX_DCHECK(vmo());

  zx_vmo_lock_state lock_state;
  zx_status_t status =
      vmo().op_range(ZX_VMO_OP_LOCK, 0, vmo_size(), &lock_state, sizeof(lock_state));
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to lock vmo for write";
  }
}

void DiscardableSound::Removelock() {
  FX_DCHECK(vmo());

  zx_status_t status = vmo().op_range(ZX_VMO_OP_UNLOCK, 0, vmo_size(), nullptr, 0);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to unlock vmo";
  }
}

zx_status_t DiscardableSound::SetSize(size_t size_arg) {
  if (vmo()) {
    return size() == size_arg ? ZX_OK : ZX_ERR_INTERNAL;
  }

  zx_status_t status = zx::vmo::create(size_arg, ZX_VMO_DISCARDABLE, &vmo());
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to create vmo";
    return status;
  }

  uint64_t vmo_size;
  FX_CHECK(vmo().get_size(&vmo_size) == ZX_OK);

  Sound::SetSize(size_arg, vmo_size);
  return ZX_OK;
}

zx_status_t DiscardableSound::SetStreamType(fuchsia::media::AudioStreamType stream_type_arg) {
  if (stream_type().frames_per_second != 0) {
    return stream_type() == stream_type_arg ? ZX_OK : ZX_ERR_INTERNAL;
  }

  Sound::SetStreamType(std::move(stream_type_arg));
  return ZX_OK;
}

void DiscardableSound::Restore() {
  if (restore_callback_) {
    restore_callback_();
  }
}

}  // namespace soundplayer
