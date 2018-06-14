// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <future>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "tests/test_support.h"

#include "bear.mpeg2.h"
#include "mpeg12_decoder.h"

static void DumpFrame(VideoFrame* frame, const char* name) {
#if DUMP_VIDEO_TO_FILE
  FILE* f = fopen(name, "a");
  io_buffer_cache_flush_invalidate(&frame->buffer, 0,
                                   frame->stride * frame->height);
  io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                   frame->stride * frame->height / 2);

  uint8_t* buf_start = (uint8_t*)io_buffer_virt(&frame->buffer);
  for (uint32_t y = 0; y < frame->height; y++) {
    fwrite(buf_start + frame->stride * y, 1, frame->width, f);
  }
  for (uint32_t y = 0; y < frame->height / 2; y++) {
    fwrite(buf_start + frame->uv_plane_offset + frame->stride * y, 1,
           frame->width, f);
  }
  fclose(f);
#endif
}

class TestMpeg2 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->EnableVideoPower();
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(true));

    video->InitializeInterrupts();
    video->video_decoder_ = std::make_unique<Mpeg12Decoder>(video.get());

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    video->video_decoder_->SetFrameReadyNotifier(
        [&frame_count, &wait_valid](VideoFrame* frame) {
          DumpFrame(frame, "/tmp/bearmpeg2.yuv");
          ++frame_count;
          if (frame_count == 28)
            wait_valid.set_value();
        });
    EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    EXPECT_EQ(ZX_OK, video->ParseVideo(bear_mpeg2, bear_mpeg2_len));

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }

  static void DecodeNoParser() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->EnableVideoPower();
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(false));

    video->InitializeInterrupts();
    video->video_decoder_ = std::make_unique<Mpeg12Decoder>(video.get());

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    video->video_decoder_->SetFrameReadyNotifier(
        [&frame_count, &wait_valid](VideoFrame* frame) {
          DumpFrame(frame, "/tmp/bearmpeg2noparser.yuv");
          ++frame_count;
          if (frame_count == 28)
            wait_valid.set_value();
        });
    EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

    video->InitializeDecoderInput();
    EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(bear_mpeg2, bear_mpeg2_len));

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }
};

TEST(MPEG2, Decode) { TestMpeg2::Decode(); }

TEST(MPEG2, DecodeNoParser) { TestMpeg2::DecodeNoParser(); }
