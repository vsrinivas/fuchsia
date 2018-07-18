// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "h264_decoder.h"
#include "tests/test_support.h"

#include "bear-larger.h264.h"
#include "bear.h264.h"
#include "macros.h"
#include "vdec1.h"

class TestH264 {
 public:
  static void Decode(bool use_parser) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    zx_status_t status = video->InitRegisters(TestSupport::parent_device());
    EXPECT_EQ(ZX_OK, status);

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();
    status = video->InitializeStreamBuffer(
        use_parser, use_parser ? PAGE_SIZE : PAGE_SIZE * 1024);
    video->InitializeInterrupts();
    EXPECT_EQ(ZX_OK, status);
    std::promise<void> first_wait_valid;
    std::promise<void> second_wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = std::make_unique<H264Decoder>(video.get());
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &first_wait_valid,
           &second_wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d width: %d height: %d\n", frame_count,
                 frame->width, frame->height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, "/tmp/bearh264.yuv");
#endif
            constexpr uint32_t kFirstVideoFrameCount = 26;
            constexpr uint32_t kSecondVideoFrameCount = 80;
            if (frame_count == kFirstVideoFrameCount)
              first_wait_valid.set_value();
            if (frame_count == kFirstVideoFrameCount + kSecondVideoFrameCount)
              second_wait_valid.set_value();
            ReturnFrame(video.get(), frame);
          });
    }

    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->InitializeEsParser());
      EXPECT_EQ(ZX_OK, video->ParseVideo(bear_h264, bear_h264_len));
    } else {
      video->core_->InitializeDirectInput();
      video->ProcessVideoNoParser(bear_h264, bear_h264_len);
    }

    EXPECT_EQ(std::future_status::ready,
              first_wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    if (use_parser) {
      EXPECT_EQ(ZX_OK,
                video->ParseVideo(bear_larger_h264, bear_larger_h264_len));
    } else {
      video->ProcessVideoNoParser(bear_larger_h264, bear_larger_h264_len);
    }

    EXPECT_EQ(std::future_status::ready,
              second_wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }

  static void DelayedReturn() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    zx_status_t status = video->InitRegisters(TestSupport::parent_device());
    EXPECT_EQ(ZX_OK, status);

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();
    status = video->InitializeStreamBuffer(true, PAGE_SIZE);
    video->InitializeInterrupts();
    EXPECT_EQ(ZX_OK, status);
    std::promise<void> wait_valid;
    // Guarded by decoder lock.
    std::vector<std::shared_ptr<VideoFrame>> frames_to_return;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = std::make_unique<H264Decoder>(video.get());
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&frames_to_return, &frame_count,
           &wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(180u, frame->display_height);
            DLOG("Got frame %d width: %d height: %d\n", frame_count,
                 frame->width, frame->height);
            constexpr uint32_t kFirstVideoFrameCount = 26;
            if (frame_count == kFirstVideoFrameCount)
              wait_valid.set_value();
            frames_to_return.push_back(frame);
          });
    }

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    EXPECT_EQ(ZX_OK, video->ParseVideo(bear_h264, bear_h264_len));

    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

    {
      DLOG("Returning frames\n");
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      for (auto frame : frames_to_return) {
        video->video_decoder_->ReturnFrame(frame);
      }
    }
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

TEST(H264, Decode) { TestH264::Decode(true); }

TEST(H264, DecodeNoParser) { TestH264::Decode(false); }

TEST(H264, DelayedReturn) { TestH264::DelayedReturn(); }
