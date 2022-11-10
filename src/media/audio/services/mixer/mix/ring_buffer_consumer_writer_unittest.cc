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
#include "src/media/audio/lib/format2/sample_converter.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});
constexpr int64_t kRingBufferFrames = 6;

struct TestHarness {
  std::shared_ptr<MemoryMappedBuffer> buffer;
  std::shared_ptr<RingBuffer> ring_buffer;
  std::unique_ptr<RingBufferConsumerWriter> writer;
};

TestHarness MakeTestHarness(Format source_format = kFormat) {
  TestHarness h;
  h.buffer = MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);
  h.ring_buffer = std::make_shared<RingBuffer>(kFormat, DefaultUnreadableClock(), h.buffer);
  h.writer = std::make_unique<RingBufferConsumerWriter>(h.ring_buffer, source_format);
  return h;
}

std::vector<float> GetPayload(TestHarness& h) {
  std::vector<float> out(kRingBufferFrames * kFormat.channels());
  std::memmove(out.data(), h.buffer->start(), kRingBufferFrames * kFormat.bytes_per_frame());
  return out;
}

TEST(RingBufferConsumerWriterTest, Write) {
  auto h = MakeTestHarness();

  std::vector<float> input = {0.0f, 0.0f, 0.1f, 0.1f, 0.2f, 0.2f, 0.3f, 0.3f,
                              0.4f, 0.4f, 0.5f, 0.5f, 0.6f, 0.6f, 0.7f, 0.7f};

  // Test WriteData.
  h.writer->WriteData(5, 3, &input[5 * kFormat.channels()]);  // {5, 6, 7}, wrapping to {5, 0, 1}
  h.writer->WriteData(2, 3, &input[2 * kFormat.channels()]);  // {2, 3, 4}

  std::vector<float> expected = {0.6f, 0.6f, 0.7f, 0.7f, 0.2f, 0.2f,
                                 0.3f, 0.3f, 0.4f, 0.4f, 0.5f, 0.5f};
  EXPECT_EQ(GetPayload(h), expected);

  // Test WriteSilence.
  h.writer->WriteSilence(4, 3);  // {4, 5, 6}, wrapping to {4, 5, 0}
  h.writer->WriteSilence(1, 3);  // {1, 2, 3}

  std::vector<float> silent(kFormat.channels() * kRingBufferFrames, 0.0f);
  EXPECT_EQ(GetPayload(h), silent);
}

TEST(RingBufferConsumerWriterTest, WriteWithConversion) {
  auto h = MakeTestHarness(
      /*source_format=*/Format::CreateOrDie({SampleType::kInt32, 2, 48000}));

  std::vector<int32_t> input_payload;
  std::vector<float> expected_payload;

  for (int k = 0; k < kRingBufferFrames * kFormat.channels(); k++) {
    input_payload.push_back(k * 100);
    expected_payload.push_back(SampleConverter<int32_t>::ToFloat(k * 100));
  }

  h.writer->WriteData(0, kRingBufferFrames, input_payload.data());
  EXPECT_EQ(GetPayload(h), expected_payload);
}

}  // namespace
}  // namespace media_audio
