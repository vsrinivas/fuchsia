// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/output_producer.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

class OutputProducerTest : public testing::Test {
 public:
  std::unique_ptr<OutputProducer> SelectOutputProducer(
      fuchsia::media::AudioSampleFormat dest_format, uint32_t num_channels) {
    fuchsia::media::AudioStreamType dest_details;
    dest_details.sample_format = dest_format;
    dest_details.channels = num_channels;
    dest_details.frames_per_second = 48000;

    return OutputProducer::Select(dest_details);
  }
};

// Create OutputProducer objects for outgoing buffers of type uint8
TEST_F(OutputProducerTest, DataFormat_8) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2));
}

// Create OutputProducer objects for outgoing buffers of type int16
TEST_F(OutputProducerTest, DataFormat_16) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_16, 4));
}

// Create OutputProducer objects for outgoing buffers of type int24-in-32
TEST_F(OutputProducerTest, DataFormat_24) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 3));
}

// Create OutputProducer objects for outgoing buffers of type float
TEST_F(OutputProducerTest, DataFormat_Float) {
  EXPECT_NE(nullptr, SelectOutputProducer(fuchsia::media::AudioSampleFormat::FLOAT, 1));
}

}  // namespace
}  // namespace media::audio
