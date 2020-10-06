// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VP9_DECODER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VP9_DECODER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include <vector>

#include "macros.h"
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
    // Multiple streams are decoded at once.
    //
    // This mode is capable of interrupting re. frame headers and decoded frames until all frames
    // previously delivered via the ring buffer are exhausted at which point the FW will interrupt
    // re. out of input.  This mode isn't very forgiving about delivering additional data that
    // doesn't contain at least the rest of a frame - the FW will hit the SW watchdog in that case.
    kMultiStream,
    // Multiple streams, each with input buffers divided on frame boundaries,
    // are decoded at once.
    //
    // This mode expects frames (originally separate or within a superframe) to be delivered
    // separately to the FW.  If multiple frames are delivered together, the FW can respond to
    // DecodeSlice with the second frame's header, instead of decoding the first frame's header,
    // which may not be usable for decoding multiple frames delivered to the FW at once.  See
    // kMultiStream for that purpose.
    kMultiFrameBased
  };
  class FrameDataProvider {
   public:
    // Called with the decoder locked.
    virtual void ReadMoreInputData(Vp9Decoder* decoder) = 0;
    virtual void ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) = 0;

    // Default behavior is for the benefit of test code; production implementation overrides all the
    // methods.
    virtual bool HasMoreInputData() { return true; }

    // CodecAdapterVp9 will fwd to CodecImpl which will async stop/start the stream (on
    // StreamControl thread) to continue decoding for the same stream, with the current input packet
    // skipped, if the stream hasn't been obsoleted by the time this request reaches the
    // StreamControl thread.
    virtual void AsyncResetStreamAfterCurrentFrame() { ZX_PANIC("not impemented"); };
  };
  enum class DecoderState {
    // In these two states the decoder is stopped because UpdateDecodeSize needs to be called. The
    // difference between these two is how it needs to be restarted.
    kInitialWaitingForInput,
    kStoppedWaitingForInput,

    // A frame was produced and the hardware is waiting for permission to decode another frame.
    kFrameJustProduced,

    // The hardware is currently processing data. The watchdog should always be running while the
    // hardware's in this state.
    kRunning,

    // The hardware is waiting for reference frames and outputs to be initialized after decoding the
    // uncompressed header and before decoding the compressed data.
    kPausedAtHeader,

    // The hardware is waiting for references frames, but the special end-of-stream size was
    // reached. It can safely be swapped out now, because its state doesn't matter.
    kPausedAtEndOfStream,

    // The hardware's state doesn't reflect that of the Vp9Decoder.
    kSwappedOut,

    // Used during watchdog handling, to avoid processing interrupts that occur after watchdog.  The
    // current decoder is deleted and a new decoder is created to take its place.
    kFailed,
  };

  Vp9Decoder(Owner* owner, Client* client, InputType input_type, bool use_compressed_output,
             bool is_secure);
  Vp9Decoder(const Vp9Decoder&) = delete;

  ~Vp9Decoder() override;

  __WARN_UNUSED_RESULT zx_status_t Initialize() override;
  __WARN_UNUSED_RESULT zx_status_t InitializeHardware() override;
  void HandleInterrupt() override;
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;
  void CallErrorHandler() override {
    have_fatal_error_ = true;
    client_->OnError();
  }
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width, uint32_t height,
                         uint32_t stride) override;
  __WARN_UNUSED_RESULT bool CanBeSwappedIn() override;
  __WARN_UNUSED_RESULT bool CanBeSwappedOut() const override {
    return state_ == DecoderState::kFrameJustProduced ||
           state_ == DecoderState::kPausedAtEndOfStream;
  }
  void SetSwappedOut() override { state_ = DecoderState::kSwappedOut; }
  void SwappedIn() override { frame_data_provider_->ReadMoreInputDataFromReschedule(this); }
  void OnSignaledWatchdog() override;
  zx_status_t SetupProtection() override;

  void SetFrameDataProvider(FrameDataProvider* provider) { frame_data_provider_ = provider; }
  void UpdateDecodeSize(uint32_t size);
  // The number of frames that have been emitted from the FW (not necessarily emitted downstream
  // however) since the most recent UpdateDecodeSize().
  uint32_t FramesSinceUpdateDecodeSize();

  __WARN_UNUSED_RESULT bool needs_more_input_data() const {
    return state_ == DecoderState::kStoppedWaitingForInput ||
           state_ == DecoderState::kInitialWaitingForInput;
  }

  __WARN_UNUSED_RESULT bool swapped_out() const { return state_ == DecoderState::kSwappedOut; }

  void SetPausedAtEndOfStream();

  void set_reallocate_buffers_next_frame_for_testing() {
    reallocate_buffers_next_frame_for_testing_ = true;
  }

  void InjectInitializationFault() { should_inject_initialization_fault_for_testing_ = true; }

 private:
  friend class Vp9UnitTest;
  friend class TestVP9;
  friend class TestFrameProvider;
  friend class CodecAdapterVp9;
  class WorkingBuffer;

  class BufferAllocator {
   public:
    void Register(WorkingBuffer* buffer);
    zx_status_t AllocateBuffers(VideoDecoder::Owner* decoder, bool is_secure);
    void CheckBuffers();

   private:
    std::vector<WorkingBuffer*> buffers_;
  };

  class WorkingBuffer {
   public:
    WorkingBuffer(BufferAllocator* allocator, size_t size, bool can_be_protected, const char* name);

    ~WorkingBuffer();

    uint32_t addr32();
    size_t size() const { return size_; }
    const char* name() const { return name_; }
    InternalBuffer& buffer() { return buffer_.value(); }
    bool has_buffer() { return buffer_.has_value(); }
    bool can_be_protected() const { return can_be_protected_; }

    void SetBuffer(InternalBuffer buffer) { buffer_.emplace(std::move(buffer)); }

   private:
    size_t size_;
    bool can_be_protected_;
    const char* name_;
    std::optional<InternalBuffer> buffer_;
  };

  struct WorkingBuffers : public BufferAllocator {
    WorkingBuffers() {}

// Sizes are large enough for 4096x2304.
#define DEF_BUFFER(name, can_be_protected, size) \
  WorkingBuffer name = WorkingBuffer(this, size, can_be_protected, #name)
    DEF_BUFFER(rpm, false, 0x400 * 2);
    DEF_BUFFER(short_term_rps, true, 0x800);
    DEF_BUFFER(picture_parameter_set, true, 0x2000);
    DEF_BUFFER(swap, true, 0x800);
    DEF_BUFFER(swap2, true, 0x800);
    DEF_BUFFER(local_memory_dump, false, 0x400 * 2);
    DEF_BUFFER(ipp_line_buffer, true, 0x4000);
    DEF_BUFFER(sao_up, true, 0x2800);
    DEF_BUFFER(scale_lut, true, 0x8000);
    // HW/firmware requires first parameters + deblock data to be adjacent in that order.
    static constexpr uint32_t kDeblockParametersSize = 0x80000;
    static constexpr uint32_t kDeblockDataSize = 0x80000;
    DEF_BUFFER(deblock_parameters, true, kDeblockParametersSize + kDeblockDataSize);
    DEF_BUFFER(deblock_parameters2, true, 0x80000);  // Only used on G12a.
    DEF_BUFFER(segment_map, true, 0xd800);
    DEF_BUFFER(probability_buffer, false, 0x1000 * 5);
    DEF_BUFFER(count_buffer, false, 0x300 * 4 * 4);
    DEF_BUFFER(motion_prediction_above, true, 0x10000);
    DEF_BUFFER(mmu_vbh, true, 0x5000);
    DEF_BUFFER(frame_map_mmu, false, 0x1200 * 4);
#undef DEF_BUFFER
  };

  struct Frame {
    Frame(Vp9Decoder* parent);
    ~Frame();

    Vp9Decoder* parent = nullptr;

    // Index into frames_.
    uint32_t index = 0;

    // This is the count of references from reference_frame_map_, last_frame_, current_frame_, and
    // any buffers the ultimate consumers have outstanding.
    int32_t refcount = 0;
    // Each VideoFrame is managed via shared_ptr<> here and via weak_ptr<> in CodecBuffer.  There is
    // a frame.reset() performed under video_decoder_lock_ that essentially signals to the
    // weak_ptr<> in CodecBuffer not to call ReturnFrame() any more for this frame.  For this
    // reason, under normal operation (not self-test), it's important that FrameReadyNotifier and
    // weak_ptr<>::lock() not result in keeping any shared_ptr<> reference on VideoFrame that lasts
    // beyond the current video_decoder_lock_ interval, since that could allow calling ReturnFrame()
    // on a frame that the Vp9Decoder doesn't want to hear about any more.
    //
    // TODO(dustingreen): Mute ReturnFrame() a different way; maybe just explicitly.  Ideally, we'd
    // use a way that's more similar between decoder self-test and "normal operation".
    //
    // This shared_ptr<> must not actually be shared outside of while video_decoder_lock_ is held.
    // See previous paragraphs.
    std::shared_ptr<VideoFrame> frame;

    // This is a frame that was received from sysmem and will next be decoded into.
    std::shared_ptr<VideoFrame> on_deck_frame;
    // With the MMU enabled the compressed frame header is stored separately from the data itself,
    // allowing the data to be allocated in noncontiguous memory.
    std::optional<InternalBuffer> compressed_header;

    io_buffer_t compressed_data = {};

    // This is decoded_frame_count_ when this frame was decoded into.
    uint32_t decoded_index = 0xffffffff;

    // This is valid even after the VideoFrame is cleared out on resize.
    uint32_t hw_width = 0;
    uint32_t hw_height = 0;
    int32_t client_refcount = 0;

    // Redueces refcount and releases |frame| if it's not necessary anymore.
    void Deref();

    // Releases |frame| if it's not currently being used as a reference frame.
    void ReleaseIfNonreference();
  };

  struct MpredBuffer {
    ~MpredBuffer();
    // This stores the motion vectors used to decode a frame for use in calculating motion vectors
    // for the next frame.
    std::optional<InternalBuffer> mv_mpred_buffer;
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
  bool FindNewFrameBuffer(HardwareRenderParams* params, bool params_checked_previously);
  void InitLoopFilter();
  void UpdateLoopFilter(HardwareRenderParams* params);
  void ProcessCompletedFrames();
  void ShowExistingFrame(HardwareRenderParams* params);
  void SkipFrameAfterFirmwareSlow();
  void PrepareNewFrame(bool params_checked_previously);
  void ConfigureFrameOutput(bool bit_depth_8);
  void ConfigureMcrcc();
  void UpdateLoopFilterThresholds();
  void ConfigureMotionPrediction();
  void ConfigureReferenceFrameHardware();
  void SetRefFrames(HardwareRenderParams* params);
  void AdaptProbabilityCoefficients(uint32_t adapt_prob_status);
  __WARN_UNUSED_RESULT zx_status_t InitializeBuffers();
  void InitializeLoopFilterData();

  InputType input_type_;

  FrameDataProvider* frame_data_provider_ = nullptr;

  WorkingBuffers working_buffers_;
  DecoderState state_ = DecoderState::kSwappedOut;
  std::unique_ptr<PowerReference> power_ref_;

  // While frames_ always has size() == kMaxFrames, the actual number of valid frames that are fully
  // usable is valid_frames_count_.  For now we don't remove any Frame from frames_ after
  // initialization, mostly for historical reasons at this point.
  //
  // TODO(dustingreen): Ensure we're getting all contig memory from sysmem, and/or always using
  // non-compressed reference frames / zero per-frame contig that isn't part of the buffer
  // collection, and if so, consider changing the size of frames_ instead of valid_frames_count_.
  uint32_t valid_frames_count_ = 0;
  std::vector<std::unique_ptr<Frame>> frames_;

  Frame* last_frame_ = nullptr;
  Frame* current_frame_ = nullptr;
  std::unique_ptr<loop_filter_info_n> loop_filter_info_;
  std::unique_ptr<loopfilter> loop_filter_;
  std::unique_ptr<segmentation> segmentation_;
  // Waiting for an available frame buffer (with reference count 0).
  bool waiting_for_empty_frames_ = false;
  // Waiting for an available output packet, to avoid show_existing_frame potentially allowing too
  // much queued output, as a show_existing_frame output frame doesn't use up a frame buffer - but
  // it does use up an output packet.  We don't directly track the output packets in the
  // h264_decoder, but this bool corresponds to being out of output packets in codec_adapter_vp9.
  // We re-try PrepareNewFrame() during ReturnFrame() even if no refcount on any Frame has reached 0
  bool waiting_for_output_ready_ = false;
  // Waiting for InitializedFrameBuffers to be called with a new size.
  bool waiting_for_new_frames_ = false;

  // This is the count of frames decoded since this object was created.
  uint32_t decoded_frame_count_ = 0;

  uint32_t frame_done_count_ = 0;

  // When we deliver a superframe containing multiple frames to the FW in one submit, the FW
  // _sometimes_ emits more than one frame per UpdateDecodeSize() + kVp9CommandNalDecodeDone.
  // Then later if we tell the FW to continue decoding with no more frames in the
  // previously-submitted data, the FW doesn't interrupt (not even with kVp9CommandNalDecodeDone)
  // and we hit the watchdog.  So instead, if the FW delivers more than one frame after
  // UpdateDecodeSize before kVp9CommandNalDecodeDone, we notice and combine the two first entries
  // in queued_frame_sizes_ to essentially remove one future UpdateDecodeSize() that's no longer
  // needed.
  uint32_t frames_since_update_decode_size_ = 0;

  // This is used to force new buffers to be allocated without needing a test stream that
  // resizes.
  bool reallocate_buffers_next_frame_for_testing_ = false;
  // This forces the next InitializeHardware call to fail.
  bool should_inject_initialization_fault_for_testing_ = false;

  PictureData last_frame_data_;
  PictureData current_frame_data_;

  std::unique_ptr<MpredBuffer> last_mpred_buffer_;
  std::unique_ptr<MpredBuffer> current_mpred_buffer_;

  // One previously-used buffer is kept around so a new buffer doesn't have to be allocated each
  // frame.
  std::unique_ptr<MpredBuffer> cached_mpred_buffer_;

  // The VP9 specification requires that 8 reference frames can be stored - they're saved in this
  // structure.
  Frame* reference_frame_map_[8] = {};

  // Each frame that's being decoded can reference 3 of the frames that are in reference_frame_map_.
  Frame* current_reference_frames_[3] = {};

  bool use_compressed_output_ = {};
  bool have_fatal_error_ = false;

  bool already_got_watchdog_ = false;

  bool has_keyframe_ = false;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VP9_DECODER_H_
