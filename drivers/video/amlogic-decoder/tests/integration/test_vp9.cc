// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "hevcdec.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"

#include "bear-vp9.aml.h"
#include "macros.h"

class TestVP9 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(true));

    video->InitializeInterrupts();

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());

    video->video_decoder_ = std::make_unique<Vp9Decoder>(video.get());
    EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    video->video_decoder_->SetFrameReadyNotifier(
        [&frame_count, &wait_valid](VideoFrame* frame) {
          ++frame_count;
          DLOG("Got frame %d\n", frame_count);
#if DUMP_VIDEO_TO_FILE
          DumpVideoFrameToFile(frame, "/tmp/bearvp9.yuv");
#endif
          if (frame_count == 1)
            wait_valid.set_value();
        });

    video->ParseVideo(bear_vp9_aml, bear_vp9_aml_len);
    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }
};
TEST(VP9, Decode) { TestVP9::Decode(); }
