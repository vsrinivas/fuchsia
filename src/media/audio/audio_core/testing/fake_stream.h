// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_STREAM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_STREAM_H_

#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"

namespace media::audio::testing {

class FakeStream : public Stream {
 public:
  FakeStream(const Format& format, size_t max_buffer_size = PAGE_SIZE);

  const fbl::RefPtr<VersionedTimelineFunction>& timeline_function() const {
    return timeline_function_;
  }

  // |media::audio::Stream|
  std::optional<Buffer> LockBuffer(zx::time now, int64_t frame, uint32_t frame_count);
  void UnlockBuffer(bool release_buffer) {}
  void Trim(zx::time trim_threshold) {}
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const;

 private:
  fbl::RefPtr<VersionedTimelineFunction> timeline_function_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>();
  size_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_STREAM_H_
