// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <random>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "video_decoder.h"

class TestFrameAllocator {
 public:
  explicit TestFrameAllocator(AmlogicVideo* video)
      : video_(video), loop_(&kAsyncLoopConfigNoAttachToCurrentThread), prng_(rd_()) {
    loop_.StartThread();
  }

  void set_decoder(VideoDecoder* decoder) {
    decoder_ = decoder;
    decoder->SetInitializeFramesHandler(
        fit::bind_member(this, &TestFrameAllocator::AllocateFrames));
  }

 private:
  zx_status_t AllocateFrames(zx::bti bti, uint32_t min_frame_count, uint32_t max_frame_count,
                             uint32_t coded_width, uint32_t coded_height, uint32_t stride,
                             uint32_t display_width, uint32_t display_height, bool has_sar,
                             uint32_t sar_width, uint32_t sar_height) {
    // Ensure client is allowed to allocate at least 2 frames for itself.
    constexpr uint32_t kMinFramesForClient = 2;
    EXPECT_LE(min_frame_count + kMinFramesForClient, max_frame_count);
    // Post to other thread so that we initialize the frames in a different callstack.
    async::PostTask(loop_.dispatcher(), [this, bti = std::move(bti), min_frame_count,
                                         max_frame_count, coded_width, coded_height, stride]() {
      std::vector<CodecFrame> frames;
      uint32_t frame_vmo_bytes = coded_height * stride * 3 / 2;
      std::uniform_int_distribution<uint32_t> frame_count_distribution(min_frame_count,
                                                                       max_frame_count);
      uint32_t frame_count = frame_count_distribution(prng_);
      LOG(INFO, "AllocateFrames() - frame_count: %u min_frame_count: %u max_frame_count: %u",
          frame_count, min_frame_count, max_frame_count);
      for (uint32_t i = 0; i < frame_count; i++) {
        zx::vmo frame_vmo;
        zx_status_t vmo_create_result =
            zx::vmo::create_contiguous(bti, frame_vmo_bytes, 0, &frame_vmo);
        if (vmo_create_result != ZX_OK) {
          DECODE_ERROR("zx_vmo_create_contiguous failed - status: %d\n", vmo_create_result);
          return;
        }
        fuchsia::media::StreamBufferData codec_buffer_data;
        fuchsia::media::StreamBufferDataVmo data_vmo;
        data_vmo.set_vmo_handle(std::move(frame_vmo));
        data_vmo.set_vmo_usable_start(0);
        data_vmo.set_vmo_usable_size(frame_vmo_bytes);
        codec_buffer_data.set_vmo(std::move(data_vmo));
        fuchsia::media::StreamBuffer buffer;
        buffer.set_buffer_lifetime_ordinal(next_non_codec_buffer_lifetime_ordinal_);
        buffer.set_buffer_index(0);
        buffer.set_data(std::move(codec_buffer_data));
        frames.emplace_back(CodecFrame{
            .codec_buffer_spec = std::move(buffer),
            .codec_buffer_ptr = nullptr,
        });
      }
      next_non_codec_buffer_lifetime_ordinal_++;
      std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
      decoder_->InitializedFrames(std::move(frames), coded_width, coded_height, stride);
    });
    return ZX_OK;
  }

  AmlogicVideo* video_ = nullptr;
  VideoDecoder* decoder_ = nullptr;
  async::Loop loop_;
  uint64_t next_non_codec_buffer_lifetime_ordinal_ = 1;
  std::random_device rd_;
  std::mt19937 prng_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_
