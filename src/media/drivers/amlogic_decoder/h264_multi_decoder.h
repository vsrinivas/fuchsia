// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_

#include <lib/fit/defer.h>

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "macros.h"
#include "media/video/h264_parser.h"
#include "media/video/h264_poc.h"
#include "registers.h"
#include "video_decoder.h"

class MultiAccelerator;
namespace media {
class H264Decoder;
class H264DPB;
struct H264PPS;
class H264Picture;
struct H264SPS;
class DecoderBuffer;
}  // namespace media

// An H264 decoder that can be context-switched in and out.
class H264MultiDecoder : public VideoDecoder {
 public:
  struct ReferenceFrame {
    bool in_use = false;
    bool in_internal_use = false;
    uint32_t index;
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;

    // TODO (use one per reference frame, rather than one per DPB frame)
    InternalBuffer reference_mv_buffer;

    uint32_t info0{};
    uint32_t info1{};
    uint32_t info2{};
    bool is_long_term_reference{};
  };
  struct SliceData {
    media::H264SPS sps;
    media::H264PPS pps;
    media::H264SliceHeader header;
    std::shared_ptr<media::H264Picture> pic;
    std::vector<std::shared_ptr<media::H264Picture>> ref_pic_list0;
    std::vector<std::shared_ptr<media::H264Picture>> ref_pic_list1;
  };
  struct DataInput {
    bool is_eos = false;

    // If input is in protected buffers, then data.empty(), but codec_buffer won't be nullptr in
    // that case.
    std::vector<uint8_t> data;
    size_t length = 0;

    std::optional<uint64_t> pts;

    const CodecBuffer* codec_buffer = nullptr;
    // Offset within codec_buffer at which data starts.  This is the offset above
    // codec_buffer->base() or codec_buffer->physical_base().
    uint32_t buffer_start_offset = 0;
    fit::deferred_callback return_input_packet;
  };
  class FrameDataProvider {
   public:
    // Called with the video_decoder_lock held.
    virtual std::optional<DataInput> ReadMoreInputData() = 0;
    virtual bool HasMoreInputData() = 0;
    virtual void AsyncPumpDecoder() = 0;
    virtual void AsyncResetStreamAfterCurrentFrame() = 0;
  };

  H264MultiDecoder(Owner* owner, Client* client, FrameDataProvider* frame_data_provider,
                   bool is_secure);
  H264MultiDecoder(const H264MultiDecoder&) = delete;

  ~H264MultiDecoder() override;

  [[nodiscard]] zx_status_t Initialize() override;
  [[nodiscard]] zx_status_t InitializeHardware() override;
  void HandleInterrupt() override;
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;
  void CallErrorHandler() override;
  // PumpOrReschedule must be called after InitializedFrames to get the decoder to continue.
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width, uint32_t height,
                         uint32_t stride) override;
  [[nodiscard]] bool CanBeSwappedIn() override;
  [[nodiscard]] bool CanBeSwappedOut() const override;
  [[nodiscard]] bool MustBeSwappedOut() const override;
  [[nodiscard]] bool ShouldSaveInputContext() const override;

  void SetSwappedOut() override;
  void SwappedIn() override;
  void OnSignaledWatchdog() override;
  zx_status_t SetupProtection() override;

  zx_status_t InitializeBuffers();

  // Signal that a the end of a stream has been reached. This will flush all frames after decoding
  // all existing frames.
  void QueueInputEos();
  void ReceivedNewInput();
  // not currently used:
  void FlushFrames();
  uint32_t GetApproximateConsumedBytes();
  void DumpStatus();
  // Try to pump the decoder, rescheduling it if it isn't currently scheduled in.
  void PumpOrReschedule();

  void SubmitDataToHardware(const uint8_t* data, size_t length, const CodecBuffer* codec_buffer,
                            uint32_t buffer_start_offset);

  // For use by MultiAccelerator.
  void SubmitSliceData(SliceData data);
  void SubmitFrameMetadata(ReferenceFrame* reference_frame, const media::H264SPS* sps,
                           const media::H264PPS* pps, const media::H264DPB& dpb);
  void OutputFrame(ReferenceFrame* reference_frame, uint32_t pts_id);
  void StartFrameDecode();
  bool IsUnusedReferenceFrameAvailable();
  std::shared_ptr<ReferenceFrame> GetUnusedReferenceFrame();
  bool is_hw_active() { return is_hw_active_; }
  bool is_decoder_started() { return is_decoder_started_; }

  void* SecondaryFirmwareVirtualAddressForTesting() { return secondary_firmware_->virt_base(); }
  void set_use_parser(bool use_parser) { use_parser_ = use_parser; }

 private:
  enum class DecoderState {
    // The hardware's state doesn't reflect that of the H264MultiDecoder.
    kSwappedOut,

    // Any stoppage waiting for input data or output surfaces.
    kWaitingForInputOrOutput,

    // After config change interrupt, waiting for new buffers.
    kWaitingForConfigChange,

    kRunning,
  };

  // This struct contains parameters for the current frame that are dumped from
  // lmem
  struct HardwareRenderParams {
    uint16_t data[0x400];
    static constexpr uint32_t kOffsetDelimiterLo = 0x2f;
    static constexpr uint32_t kOffsetDelimiterHi = 0x30;

    static constexpr uint32_t kCroppingLeftRight = 0x6a;
    static constexpr uint32_t kCroppingTopBottom = 0x6b;
    // Only newer versions of the firmware put max_num_reorder_frames here.  The current version of
    // the FW has 0 here.
    static constexpr uint32_t kMaxNumReorderFramesNewerFirmware = 0x6d;
    // jbauman points out that this may be max_dec_frame_buffering, based on how new versions of
    // the firmware put max_num_reorder_frames at 0x6d, and the two being adjacent in VUI
    // parameters.
    //
    // We've seen one test stream where this is 0 while kMaxReferenceFrameNum is 2, so probably this
    // isn't max_num_ref_frames, but it's unclear whether that stream is fully compliant or not;
    // max_dec_frame_buffering is supposed to be >= max_num_ref_frames.
    static constexpr uint32_t kMaxBufferFrame = 0x6e;

    static constexpr uint32_t kSkipPicCount = 0x74;
    static constexpr uint32_t kNewPictureStructure = 0x7c;
    static constexpr uint16_t kNewPictureStructureTopField = 0x1;
    static constexpr uint16_t kNewPictureStructureBottomField = 0x2;
    static constexpr uint16_t kNewPictureStructureFrame = 0x3;

    static constexpr uint32_t kNewIdrPicId = 0x7e;
    static constexpr uint32_t kIdrPicId = 0x7f;

    static constexpr uint32_t kNalUnitType = 0x80;
    static constexpr uint32_t kNalRefIdc = 0x81;
    static constexpr uint32_t kSliceType = 0x82;
    static constexpr uint32_t kLog2MaxFrameNum = 0x83;
    static constexpr uint32_t kFrameMbsOnlyFlag = 0x84;
    static constexpr uint32_t kPicOrderCntType = 0x85;
    static constexpr uint32_t kLog2MaxPicOrderCntLsb = 0x86;
    static constexpr uint32_t kRedundantPicCntPresentFlag = 0x88;
    static constexpr uint32_t kPicInitQpMinus26 = 0x89;
    static constexpr uint32_t kDeblockingFilterControlPresentFlag = 0x8a;
    static constexpr uint32_t kNumSliceGroupsMinus1 = 0x8b;
    static constexpr uint32_t kMode8x8Flags = 0x8c;
    static constexpr uint32_t kEntropyCodingModeFlag = 0x8d;
    static constexpr uint32_t kTotalMbHeight = 0x8f;

    static constexpr uint32_t kWeightedPredFlag = 0xa3;
    static constexpr uint32_t kWeightedBipredIdc = 0xa4;

    // bits 3..2 picture_structure
    // bit 1 mb_adaptive_frame_field_flag
    // bit 0 frame_mbs_only_flag
    static constexpr uint32_t kMbffInfo = 0xa5;

    static constexpr uint32_t kMbXNum = 0xb0;
    static constexpr uint32_t kMbWidth = 0xb1;
    static constexpr uint32_t kMbHeight = 0xb2;
    static constexpr uint32_t kMbX = 0xb3;
    static constexpr uint32_t kTotalMbY = 0xb4;
    // value is int16_t, not uint16_t
    static constexpr uint32_t kOffsetForNonRefPic = 0xe0;
    // value is int16_t, not uint16_t
    static constexpr uint32_t kOffsetForTopToBottomField = 0xe2;

    static constexpr uint32_t kMaxReferenceFrameNum = 0xe4;
    static constexpr uint32_t kFrameNumGapAllowed = 0xe5;
    static constexpr uint32_t kNumRefFramesInPicOrderCntCycle = 0xe6;
    static constexpr uint32_t kProfileIdcMmco = 0xe7;
    static constexpr uint32_t kLevelIdcMmco = 0xe8;
    static constexpr uint32_t kPpsNumRefIdxL0ActiveMinus1 = 0xeb;
    static constexpr uint32_t kPpsNumRefIdxL1ActiveMinus1 = 0xec;
    static constexpr uint32_t kCurrentSpsId = 0xed;
    static constexpr uint32_t kCurrentPpsId = 0xee;
    static constexpr uint32_t kDeltaPicOrderAlwaysZeroFlag = 0xea;

    static constexpr uint32_t kFirstMbInSlice = 0xf0;

    static constexpr uint32_t kVuiStatus = 0xf4;
    // kVuiStatus bits:
    static constexpr uint16_t kVuiStatusMaskAspectRatioInfoPresentFlag = 0x1;
    static constexpr uint16_t kVuiStatusMaskTimingInfoPresentFlag = 0x2;
    static constexpr uint16_t kVuiStatusMaskNalHrdParametersPresentFlag = 0x4;
    static constexpr uint16_t kVuiStatusMaskVclHrdParametersPresentFlag = 0x8;
    static constexpr uint16_t kVuiStatusMaskPicStructPresentFlag = 0x10;
    static constexpr uint16_t kVuiStatusMaskBitstreamRestrictionFlag = 0x20;
    static constexpr uint16_t kVuiStatusMaskAll = 0x3F;

    static constexpr uint32_t kAspectRatioIdc = 0xf5;
    static constexpr uint32_t kAspectRatioSarWidth = 0xf6;
    static constexpr uint32_t kAspectRatioSarHeight = 0xf7;
    // Observed to be zero regardless of low-latency stream or stream with frame reordering.
    static constexpr uint32_t kDpbBufferInfo = 0xfd;

    // offset to dpb_max_buffer_frame.
    static constexpr uint32_t kDpbStructStart = 0x100 + 24 * 8;

    // Observed to be zero regardless of low-latency stream or stream with frame reordering.
    static constexpr uint32_t kDpbMaxBufferFrame = kDpbStructStart + 0;
    // Observed to be zero regardless of low-latency stream or stream with frame reordering.
    static constexpr uint32_t kActualDpbSize = kDpbStructStart + 1;
    static constexpr uint32_t kPictureStructureMmco = kDpbStructStart + 12;
    static constexpr uint16_t kPictureStructureMmcoTopField = 0x0;
    static constexpr uint16_t kPictureStructureMmcoBottomField = 0x1;
    static constexpr uint16_t kPictureStructureMmcoFrame = 0x2;
    static constexpr uint16_t kPictureStructureMmcoMbaffFrame = 0x3;

    static constexpr uint32_t kFrameNum = kDpbStructStart + 13;
    static constexpr uint32_t kPicOrderCntLsb = kDpbStructStart + 14;
    static constexpr uint32_t kNumRefIdxL0ActiveMinus1 = kDpbStructStart + 15;
    static constexpr uint32_t kNumRefIdxL1ActiveMinus1 = kDpbStructStart + 16;
    // low uin16_t
    static constexpr uint32_t kDeltaPicOrderCntBottom_0 = kDpbStructStart + 19;
    // high uint16_t
    static constexpr uint32_t kDeltaPicOrderCntBottom_1 = kDpbStructStart + 20;
    // low uin16_t
    static constexpr uint32_t kDeltaPicOrderCnt0_0 = kDpbStructStart + 21;
    // high uint16_t
    static constexpr uint32_t kDeltaPicOrderCnt0_1 = kDpbStructStart + 22;
    // low uin16_t
    static constexpr uint32_t kDeltaPicOrderCnt1_0 = kDpbStructStart + 23;
    // high uint16_t
    static constexpr uint32_t kDeltaPicOrderCnt1_1 = kDpbStructStart + 24;

    // There are 128 int16_t offset_for_ref_frame values starting here, not 255,
    // not 256.  These are signed values despite data being an array of uint16_t.
    static constexpr uint32_t kOffsetForRefFrameBase = 0x200;
    static constexpr uint32_t kOffsetforRefFrameCount = 128;
    static constexpr uint32_t kMaxNumRefFramesInPicOrderCntCycle = kOffsetforRefFrameCount;

    static constexpr uint32_t kReferenceBase = kOffsetForRefFrameBase + kOffsetforRefFrameCount;
    static constexpr uint32_t kReferenceCount = 128;

    static constexpr uint32_t kL0ReorderCmdBase = kReferenceBase + kReferenceCount;
    static constexpr uint32_t kLxReorderCmdCount = 66;
    static constexpr uint32_t kL0ReorderCmdCount = kLxReorderCmdCount;

    static constexpr uint32_t kL1ReorderCmdBase = kL0ReorderCmdBase + kL0ReorderCmdCount;
    static constexpr uint32_t kL1ReorderCmdCount = kLxReorderCmdCount;

    static constexpr uint32_t kMmcoCmd = kL1ReorderCmdBase + kL1ReorderCmdCount;
    static constexpr uint32_t kMmcoCmdCount = 44;

    static constexpr uint32_t kL0Base = kMmcoCmd + kMmcoCmdCount;
    static constexpr uint32_t kLxCount = 40;
    static constexpr uint32_t kL0Count = kLxCount;

    static constexpr uint32_t kL1Base = kL0Base + kL0Count;
    static constexpr uint32_t kL1Count = kLxCount;

    // Read a pair of entries starting at |offset| as a 32-bit number.
    uint32_t Read32(uint32_t offset) {
      // Little endian.
      return data[offset] | (static_cast<uint32_t>(data[offset + 1]) << 16);
    }

    void ReadFromLmem(InternalBuffer* lmem) {
      lmem->CacheFlushInvalidate(0, sizeof(data));
      uint16_t* input_params = reinterpret_cast<uint16_t*>(lmem->virt_base());

      // Convert from middle-endian.
      for (uint32_t i = 0; i < std::size(data); i += 4) {
        for (uint32_t j = 0; j < 4; j++) {
          data[i + j] = input_params[i + (3 - j)];
        }
      }
    }
  };

  zx_status_t LoadSecondaryFirmware(const uint8_t* data, uint32_t firmware_size);
  void ResetHardware();
  void ConfigureDpb();
  void HandleSliceHeadDone();
  void HandlePicDataDone();
  void HandleBufEmpty();
  void OnFatalError();
  void PumpDecoder();
  bool InitializeRefPics(const std::vector<std::shared_ptr<media::H264Picture>>& ref_pic_list,
                         uint32_t reg_offset);
  // Output all the frames in frames_to_output.
  void OutputReadyFrames();
  void PropagatePotentialEos();
  void HandleHardwareError();
  // This method should be called when the decoder detects an error with the input stream and
  // requires that the decoder is torn down and recreated before continuing. The method will try to
  // reschedule, since the decoder can't do any more work.
  void RequestStreamReset();
  uint32_t GetStreamBufferSize();

  FrameDataProvider* frame_data_provider_;
  bool fatal_error_ = false;
  bool input_eos_queued_ = false;
  bool sent_output_eos_to_client_ = false;
  bool use_parser_ = false;

  std::unique_ptr<PowerReference> power_ref_;

  std::unique_ptr<media::H264Decoder> media_decoder_;

  std::optional<InternalBuffer> firmware_;
  std::optional<InternalBuffer> secondary_firmware_;
  std::optional<InternalBuffer> codec_data_;
  std::optional<InternalBuffer> aux_buf_;
  std::optional<InternalBuffer> lmem_;

  // HW state.  We separately track some similar SW state with bool values such as
  // waiting_for_input_.
  DecoderState state_ = DecoderState::kSwappedOut;

  // The client doesn't round-trip these so stash them here:
  uint32_t pending_display_width_ = 0;
  uint32_t pending_display_height_ = 0;

  // How the HW has been configured (not counting swap out/in):
  uint32_t hw_coded_width_ = 0;
  uint32_t hw_coded_height_ = 0;
  uint32_t hw_stride_ = 0;
  // We pretend like these are also configured in the HW even though they're not really.
  uint32_t hw_display_width_ = 0;
  uint32_t hw_display_height_ = 0;
  uint32_t hw_level_idc_ = 0;

  uint32_t next_max_reference_size_ = 0u;
  bool waiting_for_surfaces_ = false;
  bool waiting_for_input_ = false;

  // This becomes true on StartDecoding(), and becomes false on StopDecoding().
  //
  // Invariant: decoder_started_ || !hw_active_.
  bool is_decoder_started_ = false;
  // This tracks whether the FW is actively doing any decoding.
  //
  // It's possible for decoder_started_ to remain true while hw_active_ becomes false for a while,
  // then for hw_active_ to become true again without needing to StartDecoding().  For hw_active_
  // to be true, decoder_started_ must also be true.
  //
  // Invariant: decoder_started_ || !hw_active_.
  // Invariant: is_hw_active_ == watchdog is running
  bool is_hw_active_ = false;

  bool in_pump_decoder_ = false;
  bool is_async_pump_pending_ = false;

  std::vector<std::shared_ptr<ReferenceFrame>> video_frames_;
  ReferenceFrame* current_frame_ = nullptr;
  ReferenceFrame* current_metadata_frame_ = nullptr;

  std::list<uint32_t> frames_to_output_;
  // by first_mb_in_slice
  std::map<int, SliceData> slice_data_map_;
  media::H264POC poc_;
  bool have_initialized_ = false;
  uint32_t seq_info2_{};

  HardwareRenderParams params_;

  media::H264SPS current_sps_;
  media::H264PPS current_pps_;

  // This tracks which slice headers we've seen since starting from a saved state.  If we restore
  // from the same saved state later, this prevents us from telling H264Decoder about the same slice
  // more than once.
  int per_frame_seen_first_mb_in_slice_ = -1;
  int per_frame_attempt_seen_first_mb_in_slice_ = -1;

  uint32_t stream_buffer_size_ = 0;
  uint32_t stream_buffer_size_bit_count_ = 0;

  // If we fail to fully decode a frame, we'll force "swap out" but with should_save_input_context_
  // false to achieve a re-load of the old state when swapping back in.  If we succeed at fully
  // decoding a frame, we'll force_swap_out_ true in order to checkpoint the useful progress so far,
  // to persist a saved state from which we can repeatedly start from if decoding the next frame
  // doesn't work the first time due to insufficient input data so far.
  bool force_swap_out_ = false;
  // This is only true if we've made useful progress.  If we haven't made useful progress then we'll
  // restore a previous state and try to decode starting at the same read position again, with more
  // data appended at the end, and repeatedly do that until we get a picture fully decoded.
  bool should_save_input_context_ = false;

  // When we restore from a saved state we've previously started from, we want to restore everything
  // except the write pointer, since we want to keep the data written to the stream buffer last time
  // so we can re-decode all that data.
  uint64_t unwrapped_write_stream_offset_ = 0;

  // When we're adding data to the stream buffer we want to know what to avoid overwriting.  We only
  // move this forward based on the HW read pointer when we're restoring from a saved state, else
  // we may need to restore from the current saved state again which can cause the HW read offset
  // to be restored to a logically lower value.  Immediately after restoring from a saved state is
  // also when the read pointer won't reflect bytes consumed into the FIFO yet - those bytes may or
  // may not be saved along with the saved state, so we assume they aren't.  Similar to the HW read
  // pointer, this will be 512 byte aligned, only moving forward when it's safe for the parser to
  // write into bytes before this offset again.
  uint64_t unwrapped_saved_read_stream_offset_ = 0;

  // This is used to determine how many PTS values we have stored beyond the last detected slice
  // header.  We don't want to put so many packets into the stream buffer that it causes PtsManager
  // to lose track of PTS offsets within the stream buffer.
  uint64_t unwrapped_first_slice_header_of_frame_detected_stream_offset_ = 0;

  // These are used to determine if we're making progress when we're about to tell the HW to attempt
  // searching for a frame and decoding it.  If we're not making progress either adding more input
  // data or decoding a frame from input data provided so far, then we've gotten caught in a
  // pathological case involving broken input data or more PTS values from the client than makes any
  // sense.  In such cases we fail the stream (at least for now).
  uint64_t unwrapped_first_slice_header_of_frame_decoded_stream_offset_ = 0;
  uint64_t unwrapped_write_stream_offset_decode_tried_ = 0;
  uint64_t unwrapped_first_slice_header_of_frame_decoded_stream_offset_decode_tried_ = 0;

  // This points direction to a value within slice_data_map_.
  SliceData* current_slice_data_ = nullptr;
  media::H264SliceHeader stashed_latest_slice_header_;

  // Stashed during ConfigureDpb(), since not robustly available during HandleSliceHeader().
  uint32_t chroma_format_idc_ = 0;

  // The frame_num we've seen a slice header for, but not yet a pic data done.  We use this to
  // detect a missing pic data done between slices of two different frames.
  std::optional<int> frame_num_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
