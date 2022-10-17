// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>

#include <optional>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});
constexpr int64_t kRingBufferFrames = 6;

class RingBufferConsumerWriterTest : public ::testing::Test {
 public:
  MemoryMappedBuffer& buffer() { return *buffer_; }
  RingBufferConsumerWriter& writer() { return writer_; }

  std::vector<float> GetPayload() {
    std::vector<float> out(kRingBufferFrames * kFormat.channels());
    std::memmove(out.data(), buffer_->start(), kRingBufferFrames * kFormat.bytes_per_frame());
    return out;
  }

 private:
  std::shared_ptr<MemoryMappedBuffer> buffer_ =
      MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);
  std::shared_ptr<RingBuffer> ring_buffer_ =
      std::make_shared<RingBuffer>(kFormat, DefaultUnreadableClock(), buffer_);
  RingBufferConsumerWriter writer_ = RingBufferConsumerWriter(ring_buffer_);
};

TEST_F(RingBufferConsumerWriterTest, Write) {
  std::vector<float> input = {0.0f, 0.0f, 0.1f, 0.1f, 0.2f, 0.2f, 0.3f, 0.3f,
                              0.4f, 0.4f, 0.5f, 0.5f, 0.6f, 0.6f, 0.7f, 0.7f};

  // Test WriteData.
  writer().WriteData(5, 3, &input[5 * kFormat.channels()]);  // {5, 6, 7}, wrapping to {5, 0, 1}
  writer().WriteData(2, 3, &input[2 * kFormat.channels()]);  // {2, 3, 4}

  std::vector<float> expected = {0.6f, 0.6f, 0.7f, 0.7f, 0.2f, 0.2f,
                                 0.3f, 0.3f, 0.4f, 0.4f, 0.5f, 0.5f};
  EXPECT_EQ(GetPayload(), expected);

  // Test WriteSilence.
  writer().WriteSilence(4, 3);  // {4, 5, 6}, wrapping to {4, 5, 0}
  writer().WriteSilence(1, 3);  // {1, 2, 3}

  std::vector<float> silent(kFormat.channels() * kRingBufferFrames, 0.0f);
  EXPECT_EQ(GetPayload(), silent);
}

}  // namespace
}  // namespace media_audio
