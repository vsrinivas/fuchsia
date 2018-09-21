// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VP9_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VP9_DECODER_H_

#include <vector>

#include "registers.h"
#include "video_decoder.h"

// From libvpx
struct loop_filter_info_n;
struct loopfilter;
struct segmentation;

class Vp9Decoder : public VideoDecoder {
 public:
  enum class InputType {
    // A single stream is decoded at once
    kSingleStream,
    // Multiple streams are decoded at once
    kMultiStream,
    // Multiple streams, each with input buffers divided on frame boundaries,
    // are decoded at once.
    kMultiFrameBased
  };
  class FrameDataProvider {
   public:
    // Called with the decoder locked.
    virtual void ReadMoreInputData(Vp9Decoder* decoder) = 0;
    virtual void ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) = 0;
    virtual void FrameWasOutput() = 0;
    // Default behavior is for the benefit of test code; production
    // implementation overrides all the methods.
    virtual bool HasMoreInputData() { return true; }
  };
  enum class DecoderState {
    // In these two states the decoder is stopped because UpdateDecodeSize needs
    // to be called. The difference between these two is how it needs to be
    // restarted.
    kInitialWaitingForInput,
    kStoppedWaitingForInput,

    // A frame was produced and the hardware is waiting for permission to decode
    // another frame.
    kFrameJustProduced,

    // The hardware is currently processing data.
    kRunning,

    // The hardware is waiting for reference frames and outputs to be
    // initialized after decoding the uncompressed header and before decoding
    // the compressed data.
    kPausedAtHeader,

    // The hardware is waiting for references frames, but the special
    // end-of-stream size was reached. It can safely be swapped out now, because
    // its state doesn't matter.
    kPausedAtEndOfStream,

    // The hardware's state doesn't reflect that of the Vp9Decoder.
    kSwappedOut,
  };

  explicit Vp9Decoder(Owner* owner, InputType input_type);
  Vp9Decoder(const Vp9Decoder&) = delete;

  ~Vp9Decoder() override;

  __WARN_UNUSED_RESULT zx_status_t Initialize() override;
  __WARN_UNUSED_RESULT zx_status_t InitializeHardware() override;
  void HandleInterrupt() override;
  void SetFrameReadyNotifier(FrameReadyNotifier notifier) override;
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;
  void SetInitializeFramesHandler(InitializeFramesHandler handler) override;
  void SetErrorHandler(fit::closure error_handler) override;
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width,
                         uint32_t height, uint32_t stride) override;
  __WARN_UNUSED_RESULT bool CanBeSwappedIn() override;
  __WARN_UNUSED_RESULT bool CanBeSwappedOut() const override {
    return state_ == DecoderState::kFrameJustProduced ||
           state_ == DecoderState::kPausedAtEndOfStream;
  }
  void SetSwappedOut() override { state_ = DecoderState::kSwappedOut; }
  void SwappedIn() override {
    frame_data_provider_->ReadMoreInputDataFromReschedule(this);
  }

  void SetFrameDataProvider(FrameDataProvider* provider) {
    frame_data_provider_ = provider;
  }
  void UpdateDecodeSize(uint32_t size);

  __WARN_UNUSED_RESULT bool needs_more_input_data() const {
    return state_ == DecoderState::kStoppedWaitingForInput ||
           state_ == DecoderState::kInitialWaitingForInput;
  }

  __WARN_UNUSED_RESULT bool swapped_out() const {
    return state_ == DecoderState::kSwappedOut;
  }

  void SetPausedAtEndOfStream() {
    ZX_DEBUG_ASSERT(state_ == DecoderState::kPausedAtHeader);
    state_ = DecoderState::kPausedAtEndOfStream;
  }

 private:
  friend class Vp9UnitTest;
  friend class TestVP9;
  friend class TestFrameProvider;
  friend class CodecAdapterVp9;
  class WorkingBuffer;

  class BufferAllocator {
   public:
    void Register(WorkingBuffer* buffer);
    zx_status_t AllocateBuffers(VideoDecoder::Owner* decoder);
    void CheckBuffers();

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
    // HW/firmware requires first parameters + deblock data to be adjacent in
    // that order.
    static constexpr uint32_t kDeblockParametersSize = 0x80000;
    static constexpr uint32_t kDeblockDataSize = 0x80000;
    DEF_BUFFER(deblock_parameters, kDeblockParametersSize + kDeblockDataSize);
    DEF_BUFFER(deblock_parameters2, 0x80000);  // Only used on G12a.
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

    // This is decoded_frame_count_ when this frame was decoded into.
    uint32_t decoded_index = 0xffffffff;
  };

  struct MpredBuffer {
    ~MpredBuffer();
    // This stores the motion vectors used to decode a frame for use in
    // calculating motion vectors for the next frame.
    io_buffer_t mv_mpred_buffer = {};
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
  void ConfigureFrameOutput(uint32_t width, uint32_t height, bool bit_depth_8);
  void ConfigureMcrcc();
  void UpdateLoopFilterThresholds();
  void ConfigureMotionPrediction();
  void ConfigureReferenceFrameHardware();
  void SetRefFrames(HardwareRenderParams* params);
  __WARN_UNUSED_RESULT zx_status_t InitializeBuffers();
  void InitializeLoopFilterData();

  Owner* owner_;
  InputType input_type_;

  FrameDataProvider* frame_data_provider_ = nullptr;

  WorkingBuffers working_buffers_;
  FrameReadyNotifier notifier_;
  InitializeFramesHandler initialize_frames_handler_;
  fit::closure error_handler_;
  DecoderState state_ = DecoderState::kSwappedOut;

  std::vector<std::unique_ptr<Frame>> frames_;
  Frame* last_frame_ = nullptr;
  Frame* current_frame_ = nullptr;
  std::unique_ptr<loop_filter_info_n> loop_filter_info_;
  std::unique_ptr<loopfilter> loop_filter_;
  std::unique_ptr<segmentation> segmentation_ = {};
  bool waiting_for_empty_frames_ = false;

  // This is the count of frames decoded since this object was created.
  uint32_t decoded_frame_count_ = 0;

  uint32_t frame_done_count_ = 0;

  PictureData last_frame_data_;
  PictureData current_frame_data_;

  std::unique_ptr<MpredBuffer> last_mpred_buffer_;
  std::unique_ptr<MpredBuffer> current_mpred_buffer_;

  // One previously-used buffer is kept around so a new buffer doesn't have to
  // be allocated each frame.
  std::unique_ptr<MpredBuffer> cached_mpred_buffer_;

  // The VP9 specification requires that 8 reference frames can be stored -
  // they're saved in this structure.
  Frame* reference_frame_map_[8] = {};

  // Each frame that's being decoded can reference 3 of the frames that are in
  // reference_frame_map_.
  Frame* current_reference_frames_[3] = {};
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VP9_DECODER_H_
