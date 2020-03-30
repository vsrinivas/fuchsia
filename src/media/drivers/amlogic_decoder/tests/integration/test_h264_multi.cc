// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <byteswap.h>
#include <zircon/compiler.h>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "h264_multi_decoder.h"
#include "h264_utils.h"
#include "macros.h"
#include "pts_manager.h"
#include "src/lib/fxl/log_settings.h"
#include "test_frame_allocator.h"
#include "tests/test_support.h"
#include "vdec1.h"

class TestH264Multi {
 public:
  static void DecodeSetStream(const char* filename) {
    fxl::LogSettings settings;
    settings.min_log_level = -10;
    fxl::SetLogSettings(settings);
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());

    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->SetDefaultInstance(std::make_unique<H264MultiDecoder>(video.get(), &frame_allocator),
                                /*hevc=*/false);
      frame_allocator.set_decoder(video->video_decoder());
    }
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_allocator.SetFrameReadyNotifier([&video, &frame_count,
                                             &wait_valid](std::shared_ptr<VideoFrame> frame) {
        ++frame_count;
        DLOG("Got frame %d\n", frame_count);
        EXPECT_EQ(320u, frame->coded_width);
        EXPECT_EQ(192u, frame->coded_height);
#if DUMP_VIDEO_TO_FILE
        DumpVideoFrameToFile(frame.get(), filename);
#endif
        io_buffer_cache_flush_invalidate(&frame->buffer, 0, frame->stride * frame->coded_height);
        io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                         frame->stride * frame->coded_height / 2);

        uint8_t* buf_start = static_cast<uint8_t*>(io_buffer_virt(&frame->buffer));
        if (frame_count == 1) {
          // Only test a small amount for now.
          constexpr uint8_t kExpectedData[] = {124, 186, 230, 247, 252, 252, 252, 252, 252, 252};
          for (uint32_t i = 0; i < std::size(kExpectedData); ++i) {
            EXPECT_EQ(kExpectedData[i], buf_start[i]) << " index " << i;
          }
        }

        video->AssertVideoDecoderLockHeld();
        video->video_decoder()->ReturnFrame(frame);
        if (frame_count == 1) {
          wait_valid.set_value();
        }
      });

      // Initialize must happen after InitializeStreamBuffer or else it may misparse the SPS.
      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
    }

    auto bear_h264 = TestSupport::LoadFirmwareFile("video_test_data/bear.h264");
    ASSERT_NE(nullptr, bear_h264);
    video->core()->InitializeDirectInput();
    auto nal_units = SplitNalUnits(bear_h264->ptr, bear_h264->size);
    for (auto& nal_unit : nal_units) {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->ProcessNalUnit(std::move(nal_unit));
    }

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(1)));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->DumpStatus();
    }

    EXPECT_LE(1u, frame_count);

    video->ClearDecoderInstance();
    video.reset();
  }
};

TEST(H264Multi, DecodeSetStream) { TestH264Multi::DecodeSetStream("/tmp/bearmultih264.yuv"); }
