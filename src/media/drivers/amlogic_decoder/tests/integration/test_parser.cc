// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/test_support.h"

class TestParser {
 public:
  static void MemoryCopy(uint32_t input_data_size) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->UngateParserClock();

    io_buffer_t output_buffer = {};
    constexpr uint32_t kOutputDataPadding = 0x80;
    uint32_t output_buffer_size = input_data_size + kOutputDataPadding;

    ASSERT_EQ(ZX_OK, video->AllocateIoBuffer(&output_buffer, output_buffer_size, 0,
                                             IO_BUFFER_CONTIG | IO_BUFFER_RW, "testoutput"));

    uint8_t* output_data = static_cast<uint8_t*>(io_buffer_virt(&output_buffer));
    memset(output_data + input_data_size, 0xff, kOutputDataPadding);
    io_buffer_cache_flush(&output_buffer, 0, output_buffer_size);
    EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    video->parser()->SetOutputLocation(io_buffer_phys(&output_buffer), output_buffer_size);

    uint8_t input_data[input_data_size];
    for (uint32_t i = 0; i < input_data_size; i++) {
      input_data[i] = i & 0xff;
    }
    EXPECT_EQ(ZX_OK, video->parser()->ParseVideo(input_data, input_data_size));

    EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(10)));

    io_buffer_cache_flush_invalidate(&output_buffer, 0, input_data_size);
    for (uint32_t i = 0; i < input_data_size; i++) {
      if (input_data[i] != output_data[i]) {
        EXPECT_EQ(input_data[i], output_data[i])
            << "Input " << input_data[i] << " not equal to " << output_data[i] << " location " << i;
        break;
      }
    }
    for (uint32_t i = input_data_size; i < output_buffer_size; i++) {
      if (output_data[i] != 0xff) {
        EXPECT_EQ(0xff, output_data[i])
            << "Location " << i << " incorrectly modified " << output_data[i];
        break;
      }
    }
    io_buffer_release(&output_buffer);

    video.reset();
  }
};

TEST(Parser, MemoryCopy4079) { TestParser::MemoryCopy(4079); }
TEST(Parser, MemoryCopy4080) { TestParser::MemoryCopy(4080); }
TEST(Parser, MemoryCopy4096) { TestParser::MemoryCopy(4096); }
