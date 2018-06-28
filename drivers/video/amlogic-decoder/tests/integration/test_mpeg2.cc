// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <future>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "tests/test_support.h"
#include "vdec1.h"

#include "bear.mpeg2.h"
#include "mpeg12_decoder.h"

class TestMpeg2 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(true));

    video->InitializeInterrupts();
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = std::make_unique<Mpeg12Decoder>(video.get());

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count,
           &wait_valid](std::shared_ptr<VideoFrame> frame) {
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, "/tmp/bearmpeg2.yuv");
#endif
            ++frame_count;
            if (frame_count == 28)
              wait_valid.set_value();
            ReturnFrame(video.get(), frame);
          });
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

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

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(false));

    video->InitializeInterrupts();
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = std::make_unique<Mpeg12Decoder>(video.get());

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count,
           &wait_valid](std::shared_ptr<VideoFrame> frame) {
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, "/tmp/bearmpeg2noparser.yuv");
#endif
            ++frame_count;
            if (frame_count == 28)
              wait_valid.set_value();
            ReturnFrame(video.get(), frame);
          });
    EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }
    video->core_->InitializeDirectInput();
    video->ProcessVideoNoParser(bear_mpeg2, bear_mpeg2_len);

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }

 private:
  // This is called from the interrupt handler, which already holds the lock.
  static void ReturnFrame(AmlogicVideo* video,
                          std::shared_ptr<VideoFrame> frame)
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    video->video_decoder_->ReturnFrame(frame);
  }
};

TEST(MPEG2, Decode) { TestMpeg2::Decode(); }

TEST(MPEG2, DecodeNoParser) { TestMpeg2::DecodeNoParser(); }
