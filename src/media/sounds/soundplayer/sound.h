// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_SOUNDS_SOUNDPLAYER_SOUND_H_
#define SRC_MEDIA_SOUNDS_SOUNDPLAYER_SOUND_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/vmo.h>

namespace soundplayer {

class Sound {
 public:
  Sound(zx::vmo vmo, uint64_t size, fuchsia::media::AudioStreamType stream_type);

  Sound();

  Sound(Sound&& other) noexcept;

  ~Sound();

  Sound& operator=(Sound&& other) noexcept;

  const zx::vmo& vmo() const { return vmo_; }

  uint64_t size() const { return size_; }

  const fuchsia::media::AudioStreamType& stream_type() const { return stream_type_; }

  zx::duration duration() const;

  uint64_t frame_count() const;

  uint32_t frame_size() const;

  uint32_t sample_size() const;

 private:
  zx::vmo vmo_;
  uint64_t size_;
  fuchsia::media::AudioStreamType stream_type_;
};

}  // namespace soundplayer

#endif  // SRC_MEDIA_SOUNDS_SOUNDPLAYER_SOUND_H_
