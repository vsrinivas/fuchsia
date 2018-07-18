// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VP9_DECODER_H_
#define VP9_DECODER_H_

#include <vector>

#include "registers.h"
#include "video_decoder.h"

// From libvpx
struct loop_filter_info_n;
struct loopfilter;
struct segmentation;

class Vp9Decoder : public VideoDecoder {
 public:
  explicit Vp9Decoder(Owner* owner);
  Vp9Decoder(const Vp9Decoder&) = delete;

  ~Vp9Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  void SetFrameReadyNotifier(FrameReadyNotifier notifier) override;
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;

 private:
  friend class Vp9UnitTest;
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

    // Index into frames_.
    uint32_t index;

    // This is the count of references from reference_frame_map_, last_frame_,
    // current_frame_, and any buffers the ultimate consumers have outstanding.
    int32_t refcount = 0;
    // Allocated on demand.
    std::shared_ptr<VideoFrame> frame;
    // With the MMU enabled the compressed frame header is stored separately
    // from the data itself, allowing the data to be allocated in noncontiguous
    // memory.
    io_buffer_t compressed_header = {};

    io_buffer_t compressed_data = {};

    // This stores the motion vectors used to decode this frame for use in
    // calculating motion vectors for the next frame.
    io_buffer_t mv_mpred_buffer = {};

    // This is decoded_frame_count_ when this frame was decoded into.
    uint32_t decoded_index = 0xffffffff;
  };

  struct PictureData {
    bool keyframe = false;
    bool intra_only = false;
    uint32_t refresh_frame_flags = 0;
    bool show_frame;
    bool error_resilient_mode;
    bool has_pts = false;
    uint64_t pts = 0;
  };

  union HardwareRenderParams;

  zx_status_t AllocateFrames();
  void InitializeHardwarePictureList();
  void InitializeParser();
  bool FindNewFrameBuffer(HardwareRenderParams* params);
  void InitLoopFilter();
  void UpdateLoopFilter(HardwareRenderParams* params);
  void ProcessCompletedFrames();
  void ShowExistingFrame(HardwareRenderParams* params);
  void PrepareNewFrame();
  void ConfigureFrameOutput(uint32_t width, uint32_t height);
  void ConfigureMcrcc();
  void UpdateLoopFilterThresholds();
  void ConfigureMotionPrediction();
  void ConfigureReferenceFrameHardware();
  void SetRefFrames(HardwareRenderParams* params);
  zx_status_t InitializeBuffers();
  zx_status_t InitializeHardware();
  void InitializeLoopFilterData();

  Owner* owner_;

  WorkingBuffers working_buffers_;
  FrameReadyNotifier notifier_;

  std::vector<std::unique_ptr<Frame>> frames_;
  Frame* last_frame_ = nullptr;
  Frame* current_frame_ = nullptr;
  std::unique_ptr<loop_filter_info_n> loop_filter_info_;
  std::unique_ptr<loopfilter> loop_filter_;
  std::unique_ptr<segmentation> segmentation_ = {};
  bool waiting_for_empty_frames_ = false;

  // This is the count of frames decoded since this object was created.
  uint32_t decoded_frame_count_ = 0;

  PictureData last_frame_data_;
  PictureData current_frame_data_;
  // The VP9 specification requires that 8 reference frames can be stored -
  // they're saved in this structure.
  Frame* reference_frame_map_[8] = {};

  // Each frame that's being decoded can reference 3 of the frames that are in
  // reference_frame_map_.
  Frame* current_reference_frames_[3] = {};
};

#endif  // VP9_DECODER_H_
