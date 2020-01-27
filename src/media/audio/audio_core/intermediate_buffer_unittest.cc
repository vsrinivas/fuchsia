// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/intermediate_buffer.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

static const Format kFormat = Format({
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
});

class IntermediateBufferTest : public ::testing::Test {};

TEST_F(IntermediateBufferTest, LockBuffer) {
  auto one_frame_per_ms =
      TimelineFunction(TimelineRate(FractionalFrames<uint32_t>(1).raw_value(), 1'000'000));
  auto intermediate_buffer = std::make_shared<IntermediateBuffer>(kFormat, 256, one_frame_per_ms);
  ASSERT_TRUE(intermediate_buffer);

  ASSERT_NE(intermediate_buffer->buffer(), nullptr);
  ASSERT_EQ(intermediate_buffer->frame_count(), 256u);

  {
    auto stream_buffer = intermediate_buffer->LockBuffer(zx::time(0), 0, 256);
    ASSERT_TRUE(stream_buffer);
    ASSERT_EQ(intermediate_buffer->buffer(), stream_buffer->payload());
    ASSERT_EQ(FractionalFrames<int64_t>(0), stream_buffer->start());
    ASSERT_EQ(FractionalFrames<uint32_t>(256), stream_buffer->length());
  }

  {
    auto stream_buffer = intermediate_buffer->LockBuffer(zx::time(0), 3, 256);
    ASSERT_TRUE(stream_buffer);
    ASSERT_EQ(intermediate_buffer->buffer(), stream_buffer->payload());
    ASSERT_EQ(FractionalFrames<int64_t>(3), stream_buffer->start());
    ASSERT_EQ(FractionalFrames<uint32_t>(256), stream_buffer->length());
  }
}

TEST_F(IntermediateBufferTest, ClampLengthToBufferSize) {
  auto one_frame_per_ms =
      TimelineFunction(TimelineRate(FractionalFrames<uint32_t>(1).raw_value(), 1'000'000));
  auto intermediate_buffer = std::make_shared<IntermediateBuffer>(kFormat, 256, one_frame_per_ms);
  ASSERT_TRUE(intermediate_buffer);

  // Request 1024 frames, but since the buffer is only 256 frames the returned buffer should be
  // truncated to 256 frames.
  auto stream_buffer = intermediate_buffer->LockBuffer(zx::time(0), 0, 1024);
  ASSERT_TRUE(stream_buffer);
  ASSERT_EQ(intermediate_buffer->buffer(), stream_buffer->payload());
  ASSERT_EQ(FractionalFrames<int64_t>(0), stream_buffer->start());
  ASSERT_EQ(FractionalFrames<uint32_t>(256), stream_buffer->length());
}

}  // namespace
}  // namespace media::audio
