// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "amlogic-video.h"
#include "video_decoder.h"

class TestFrameAllocator {
 public:
  explicit TestFrameAllocator(AmlogicVideo* video)
      : video_(video), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread();
  }

  void set_decoder(VideoDecoder* decoder) {
    decoder_ = decoder;
    decoder->SetInitializeFramesHandler(
        fit::bind_member(this, &TestFrameAllocator::AllocateFrames));
  }

 private:
  zx_status_t AllocateFrames(zx::bti bti, uint32_t frame_count, uint32_t coded_width,
                             uint32_t coded_height, uint32_t stride, uint32_t display_width,
                             uint32_t display_height, bool has_sar, uint32_t sar_width,
                             uint32_t sar_height) {
    // Post to other thread so that we initialize the frames in a different callstack.
    async::PostTask(loop_.dispatcher(), [this, bti = std::move(bti), frame_count, coded_width,
                                         coded_height, stride]() {
      std::vector<CodecFrame> frames;
      uint32_t frame_vmo_bytes = coded_height * stride * 3 / 2;
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

  AmlogicVideo* video_;
  VideoDecoder* decoder_ = nullptr;
  async::Loop loop_;
  uint64_t next_non_codec_buffer_lifetime_ordinal_ = 1;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_INTEGRATION_TEST_FRAME_ALLOCATOR_H_
