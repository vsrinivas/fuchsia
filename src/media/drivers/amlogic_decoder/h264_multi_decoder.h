// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_

#include <lib/fit/defer.h>

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
  bool currently_decoding() { return currently_decoding_; }

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
    static constexpr uint32_t kDpbBufferInfo = 0xfd;

    // offset to dpb_max_buffer_frame.
    static constexpr uint32_t kDpbStructStart = 0x100 + 24 * 8;

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

  FrameDataProvider* frame_data_provider_;
  bool fatal_error_ = false;
  bool input_eos_queued_ = false;
  bool sent_output_eos_to_client_ = false;
  bool use_parser_ = false;

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
  bool currently_decoding_ = false;
  bool in_pump_decoder_ = false;
  bool is_async_pump_pending_ = false;

  std::vector<std::shared_ptr<ReferenceFrame>> video_frames_;
  ReferenceFrame* current_frame_ = nullptr;
  ReferenceFrame* current_metadata_frame_ = nullptr;

  std::list<uint32_t> frames_to_output_;
  std::list<SliceData> slice_data_list_;
  media::H264POC poc_;
  bool have_initialized_ = false;
  uint32_t seq_info2_{};
  // This is the index of the next bitstream id to be assigned to an input buffer.
  uint32_t next_pts_id_{};

  // |id_to_pts_map_| maps from bitstream ids to PTSes. Bitstream IDs are assigned to input buffers
  // and media::H264Decoder plumbs them through to the resulting H264Pictures.
  std::unordered_map<uint32_t, uint64_t> id_to_pts_map_;
  HardwareRenderParams params_;

  media::H264SPS current_sps_;
  media::H264PPS current_pps_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
