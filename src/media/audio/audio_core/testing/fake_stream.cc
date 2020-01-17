// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/fake_stream.h"

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio::testing {

FakeStream::FakeStream(const Format& format, size_t max_buffer_size) : Stream(format) {
  buffer_size_ = max_buffer_size;
  buffer_ = std::make_unique<uint8_t[]>(buffer_size_);
  memset(buffer_.get(), 0, buffer_size_);
}

std::optional<Stream::Buffer> FakeStream::LockBuffer(zx::time now, int64_t frame,
                                                     uint32_t frame_count) {
  FX_CHECK(frame_count * format().bytes_per_frame() < buffer_size_);
  return {Stream::Buffer(FractionalFrames<int64_t>(frame), FractionalFrames<uint32_t>(frame_count),
                         buffer_.get(), true)};
}

Stream::TimelineFunctionSnapshot FakeStream::ReferenceClockToFractionalFrames() const {
  auto [timeline_function, generation] = timeline_function_->get();
  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

}  // namespace media::audio::testing
