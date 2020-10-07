// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CACHED_READABLE_STREAM_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CACHED_READABLE_STREAM_BUFFER_H_

#include <optional>

#include "src/media/audio/audio_core/stream.h"

namespace media::audio {

class CachedReadableStreamBuffer {
 public:
  // Reports whether the current cached buffer contains the given frame.
  bool Contains(Fixed frame) {
    return cached_ && cached_->start() <= frame && frame < cached_->end();
  }

  // Discard the current cached buffer.
  void Reset() { cached_ = std::nullopt; }

  // Take ownership of the given buffer.
  void Set(ReadableStream::Buffer&& buffer) { cached_ = std::move(buffer); }

  // Get the current cached buffer. Once this is called, it should not be called
  // again until the returned buffer is destructed. If the returned buffer is fully
  // consumed, the cache will be reset.
  ReadableStream::Buffer Get() {
    FX_CHECK(cached_);
    FX_CHECK(!has_dup_);
    has_dup_ = true;
    return ReadableStream::Buffer(cached_->start(), cached_->length(), cached_->payload(),
                                  cached_->is_continuous(), cached_->usage_mask(),
                                  cached_->gain_db(), [this](bool fully_consumed) {
                                    has_dup_ = false;
                                    if (fully_consumed) {
                                      Reset();
                                    }
                                  });
  }

 private:
  std::optional<ReadableStream::Buffer> cached_;
  bool has_dup_ = false;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CACHED_READABLE_STREAM_BUFFER_H_
