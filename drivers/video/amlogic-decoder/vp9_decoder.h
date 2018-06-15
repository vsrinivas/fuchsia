// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VP9_DECODER_H_
#define VP9_DECODER_H_

#include <vector>

#include "registers.h"
#include "video_decoder.h"

class Vp9Decoder : public VideoDecoder {
 public:
  Vp9Decoder(Owner* owner) : owner_(owner) {}

  ~Vp9Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  void SetFrameReadyNotifier(FrameReadyNotifier notifier) override;

 private:
  class WorkingBuffer;

  class BufferAllocator {
   public:
    void Register(WorkingBuffer* buffer);
    zx_status_t AllocateBuffers(VideoDecoder::Owner* decoder);

   private:
    std::vector<WorkingBuffer*> buffers_;
  };

  class WorkingBuffer {
   public:
    WorkingBuffer(BufferAllocator* allocator, size_t size);

    ~WorkingBuffer();

    uint32_t addr32();
    size_t size() const { return size_; }
    io_buffer_t& buffer() { return buffer_; }

   private:
    size_t size_;
    io_buffer_t buffer_ = {};
  };

  struct WorkingBuffers : public BufferAllocator {
    WorkingBuffers() {}

// Sizes are large enough for 4096x2304.
#define DEF_BUFFER(name, size) WorkingBuffer name = WorkingBuffer(this, size)
    DEF_BUFFER(rpm, 0x400 * 2);
    DEF_BUFFER(short_term_rps, 0x800);
    DEF_BUFFER(picture_parameter_set, 0x2000);
    DEF_BUFFER(swap, 0x800);
    DEF_BUFFER(swap2, 0x800);
    DEF_BUFFER(local_memory_dump, 0x400 * 2);
    DEF_BUFFER(ipp_line_buffer, 0x4000);
    DEF_BUFFER(sao_up, 0x2800);
    DEF_BUFFER(scale_lut, 0x8000);
    DEF_BUFFER(deblock_data, 0x80000);
    DEF_BUFFER(deblock_data2, 0x80000);
    DEF_BUFFER(deblock_parameters, 0x80000);
    DEF_BUFFER(segment_map, 0xd800);
    DEF_BUFFER(probability_buffer, 0x1000 * 5);
    DEF_BUFFER(count_buffer, 0x300 * 4 * 4);
    DEF_BUFFER(motion_prediction_above, 0x10000);
    DEF_BUFFER(mmu_vbh, 0x5000);
    DEF_BUFFER(frame_map_mmu, 0x1200 * 4);
#undef DEF_BUFFER
  };

  struct Frame {
    ~Frame();

    // Allocated on demand.
    std::unique_ptr<VideoFrame> frame;
    // With the MMU enabled the compressed frame header is stored separately
    // from the data itself, allowing the data to be allocated in noncontiguous
    // memory.
    io_buffer_t compressed_header = {};
  };

  zx_status_t AllocateFrames();
  void InitializeHardwarePictureList();
  void InitializeParser();

  Owner* owner_;

  WorkingBuffers working_buffers_;
  FrameReadyNotifier notifier_;

  std::vector<std::unique_ptr<Frame>> frames_;
};

#endif  // VP9_DECODER_H_
