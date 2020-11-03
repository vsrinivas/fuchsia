// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>

#include <random>

#include <gtest/gtest.h>

#include "amlogic-video.h"
#include "macros.h"
#include "tests/test_basic_client.h"
#include "video_decoder.h"

class TestFrameAllocator : public TestBasicClient {
 public:
  explicit TestFrameAllocator(AmlogicVideo* video)
      : video_(video), loop_(&kAsyncLoopConfigNoAttachToCurrentThread), prng_(rd_()) {
    loop_.StartThread();
  }

  void set_decoder(VideoDecoder* decoder) { decoder_ = decoder; }

  void set_use_minimum_frame_count(bool use_minimum) { use_minimum_frame_count_ = use_minimum; }
  void set_pump_function(fit::closure pump_function) { pump_function_ = std::move(pump_function); }
  bool has_sar() const { return has_sar_; }

  zx_status_t InitializeFrames(uint32_t min_frame_count, uint32_t max_frame_count,
                               uint32_t coded_width, uint32_t coded_height, uint32_t stride,
                               uint32_t display_width, uint32_t display_height, bool has_sar,
                               uint32_t sar_width, uint32_t sar_height) override {
    // Ensure client is allowed to allocate at least 2 frames for itself.
    constexpr uint32_t kMinFramesForClient = 2;
    EXPECT_LE(min_frame_count + kMinFramesForClient, max_frame_count);
    has_sar_ = has_sar;
    // Post to other thread so that we initialize the frames in a different callstack.
    async::PostTask(loop_.dispatcher(), [this, min_frame_count, max_frame_count, coded_width,
                                         coded_height, stride]() {
      std::vector<CodecFrame> frames;
      uint32_t frame_vmo_bytes = coded_height * stride * 3 / 2;
      std::uniform_int_distribution<uint32_t> frame_count_distribution(min_frame_count,
                                                                       max_frame_count);
      uint32_t frame_count =
          use_minimum_frame_count_ ? min_frame_count : frame_count_distribution(prng_);
      LOG(INFO, "AllocateFrames() - frame_count: %u min_frame_count: %u max_frame_count: %u",
          frame_count, min_frame_count, max_frame_count);
      for (uint32_t i = 0; i < frame_count; i++) {
        zx::vmo frame_vmo;
        zx_status_t vmo_create_result =
            zx::vmo::create_contiguous(*video_->bti(), frame_vmo_bytes, 0, &frame_vmo);
        if (vmo_create_result != ZX_OK) {
          DECODE_ERROR("zx_vmo_create_contiguous failed - status: %d", vmo_create_result);
          return;
        }
        frame_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, frame_vmo_bytes, nullptr, 0);

        frames.emplace_back(CodecFrame::BufferSpec{
            .buffer_lifetime_ordinal = next_non_codec_buffer_lifetime_ordinal_,
            .buffer_index = 0,
            .vmo_range = CodecVmoRange(std::move(frame_vmo), 0, frame_vmo_bytes)});
      }
      next_non_codec_buffer_lifetime_ordinal_++;
      {
        std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
        decoder_->InitializedFrames(std::move(frames), coded_width, coded_height, stride);
      }
      if (pump_function_) {
        pump_function_();
      }
    });
    return ZX_OK;
  }

 private:
  AmlogicVideo* video_ = nullptr;
  VideoDecoder* decoder_ = nullptr;
  async::Loop loop_;
  uint64_t next_non_codec_buffer_lifetime_ordinal_ = 1;
  std::random_device rd_;
  std::mt19937 prng_;
  bool use_minimum_frame_count_ = false;
  fit::closure pump_function_;
  bool has_sar_ = false;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_
