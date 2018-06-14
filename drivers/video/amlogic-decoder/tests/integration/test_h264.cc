// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "h264_decoder.h"
#include "tests/test_support.h"

#include "bear.h264.h"
#include "macros.h"
#include "vdec1.h"

class TestH264 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    zx_status_t status = video->InitRegisters(TestSupport::parent_device());
    EXPECT_EQ(ZX_OK, status);

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();
    status = video->InitializeStreamBuffer(true);
    video->InitializeInterrupts();
    EXPECT_EQ(ZX_OK, status);
    video->video_decoder_ = std::make_unique<H264Decoder>(video.get());
    EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    video->video_decoder_->SetFrameReadyNotifier(
        [&frame_count, &wait_valid](VideoFrame* frame) {
          ++frame_count;
          DLOG("Got frame %d\n", frame_count);
#if DUMP_VIDEO_TO_FILE
          DumpVideoFrameToFile(frame, "/tmp/bearh264.yuv");
#endif
          if (frame_count == 26)
            wait_valid.set_value();
        });

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    EXPECT_EQ(ZX_OK, video->ParseVideo(bear_h264, bear_h264_len));

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }

  static void DecodeNoParser() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    zx_status_t status = video->InitRegisters(TestSupport::parent_device());
    EXPECT_EQ(ZX_OK, status);

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();
    status = video->InitializeStreamBuffer(false);
    video->InitializeInterrupts();
    EXPECT_EQ(ZX_OK, status);
    video->video_decoder_ = std::make_unique<H264Decoder>(video.get());
    EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    video->video_decoder_->SetFrameReadyNotifier(
        [&frame_count, &wait_valid](VideoFrame* frame) {
          ++frame_count;
          DLOG("Got frame %d\n", frame_count);
#if DUMP_VIDEO_TO_FILE
          DumpVideoFrameToFile(frame, "/tmp/bearh264noparser.yuv");
#endif
          if (frame_count == 26)
            wait_valid.set_value();
        });

    video->core_->InitializeDirectInput();
    video->ProcessVideoNoParser(bear_h264, bear_h264_len);

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }
};

TEST(H264, Decode) { TestH264::Decode(); }

TEST(H264, DecodeNoParser) { TestH264::DecodeNoParser(); }
