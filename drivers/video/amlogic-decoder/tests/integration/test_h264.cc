// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "h264_decoder.h"
#include "tests/test_support.h"

#include "macros.h"
#include "vdec1.h"

std::vector<std::vector<uint8_t>> SplitNalUnits(const uint8_t* start_data,
                                                uint32_t size) {
  std::vector<std::vector<uint8_t>> out_vector;

  const uint8_t* this_nal_start = start_data;
  while (true) {
    if (size < 3)
      return out_vector;
    uint8_t start_code[3] = {0, 0, 1};
    // Add 2 to ensure the next start code found isn't the start of this nal
    // unit.
    uint8_t* next_nal_start = static_cast<uint8_t*>(
        memmem(this_nal_start + 2, size - 2, start_code, sizeof(start_code)));
    if (next_nal_start && next_nal_start[-1] == 0)
      next_nal_start--;
    uint32_t data_size =
        next_nal_start ? next_nal_start - this_nal_start : size;
    if (data_size > 0) {
      std::vector<uint8_t> new_data(data_size);
      memcpy(new_data.data(), this_nal_start, data_size);
      out_vector.push_back(std::move(new_data));
    }

    if (!next_nal_start) {
      return out_vector;
    }

    size -= data_size;
    this_nal_start = next_nal_start;
  }
}

class TestH264 {
 public:
  static void Decode(bool use_parser) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    auto bear_h264 = TestSupport::LoadFirmwareFile("video_test_data/bear.h264");
    ASSERT_NE(nullptr, bear_h264);
    auto larger_h264 =
        TestSupport::LoadFirmwareFile("video_test_data/test-25fps.h264");
    ASSERT_NE(nullptr, larger_h264);
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
      EXPECT_EQ(ZX_OK, video->ParseVideo(bear_h264->ptr, bear_h264->size));
    } else {
      video->core_->InitializeDirectInput();
      video->ProcessVideoNoParser(bear_h264->ptr, bear_h264->size);
    }

    EXPECT_EQ(std::future_status::ready,
              first_wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->ParseVideo(larger_h264->ptr, larger_h264->size));
    } else {
      video->ProcessVideoNoParser(larger_h264->ptr, larger_h264->size);
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

    auto bear_h264 = TestSupport::LoadFirmwareFile("video_test_data/bear.h264");
    ASSERT_NE(nullptr, bear_h264);
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
    auto as = std::async([&video, &bear_h264]() {
      EXPECT_EQ(ZX_OK, video->ParseVideo(bear_h264->ptr, bear_h264->size));
    });

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

    as.wait();
    video.reset();
  }

  static void DecodeNalUnits(bool use_parser) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    zx_status_t status = video->InitRegisters(TestSupport::parent_device());
    EXPECT_EQ(ZX_OK, status);
    auto bear_h264 = TestSupport::LoadFirmwareFile("video_test_data/bear.h264");
    ASSERT_NE(nullptr, bear_h264);

    video->core_ = std::make_unique<Vdec1>(video.get());
    video->core_->PowerOn();
    status = video->InitializeStreamBuffer(
        use_parser, use_parser ? PAGE_SIZE : PAGE_SIZE * 1024);
    video->InitializeInterrupts();
    EXPECT_EQ(ZX_OK, status);
    std::promise<void> first_wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = std::make_unique<H264Decoder>(video.get());
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count,
           &first_wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d width: %d height: %d\n", frame_count,
                 frame->width, frame->height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, "/tmp/bearh264.yuv");
#endif
            constexpr uint32_t kFirstVideoFrameCount = 26;
            if (frame_count == kFirstVideoFrameCount)
              first_wait_valid.set_value();
            ReturnFrame(video.get(), frame);
          });
    }

    auto split_nal = SplitNalUnits(bear_h264->ptr, bear_h264->size);
    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->InitializeEsParser());
      uint32_t total_size = 0;
      for (auto& nal : split_nal) {
        total_size += nal.size();
      }
      EXPECT_EQ(bear_h264->size, total_size);
      for (auto& nal : split_nal) {
        EXPECT_EQ(ZX_OK, video->ParseVideo(nal.data(), nal.size()));
      }
    } else {
      video->core_->InitializeDirectInput();
      for (auto& nal : split_nal) {
        EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(nal.data(), nal.size()));
      }
    }

    EXPECT_EQ(std::future_status::ready,
              first_wait_valid.get_future().wait_for(std::chrono::seconds(1)));

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

TEST(H264, DecodeNalUnits) { TestH264::DecodeNalUnits(true); }

TEST(H264, DecodeNalUnitsNoParser) { TestH264::DecodeNalUnits(false); }
