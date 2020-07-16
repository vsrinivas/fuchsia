// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_V1_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_V1_H_

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
class H264MultiDecoderV1 : public VideoDecoder {
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
    std::vector<uint8_t> data;
    std::optional<uint64_t> pts;
  };
  class FrameDataProvider {
   public:
    // Called with the video_decoder_lock held.
    virtual DataInput ReadMoreInputData() = 0;
    virtual bool HasMoreInputData() = 0;
    virtual void AsyncResetStreamAfterCurrentFrame() = 0;
  };

  H264MultiDecoderV1(Owner* owner, Client* client, FrameDataProvider* frame_data_provider,
                     bool is_secure);
  H264MultiDecoderV1(const H264MultiDecoderV1&) = delete;

  ~H264MultiDecoderV1() override;

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
  void FlushFrames();
  void DumpStatus();
  // Try to pump the decoder, rescheduling it if it isn't currently scheduled in.
  void PumpOrReschedule();

  // For use by MultiAccelerator.
  void SubmitDataToHardware(const uint8_t* data, size_t length);
  void SubmitSliceData(SliceData data);
  void SubmitFrameMetadata(ReferenceFrame* reference_frame, const media::H264SPS* sps,
                           const media::H264PPS* pps, const media::H264DPB& dpb);
  void OutputFrame(ReferenceFrame* reference_frame, uint32_t pts_id);
  void StartFrameDecode();
  std::shared_ptr<ReferenceFrame> GetUnusedReferenceFrame();
  bool currently_decoding() { return currently_decoding_; }

  void* SecondaryFirmwareVirtualAddressForTesting() { return secondary_firmware_->virt_base(); }
  void set_use_parser(bool use_parser) { use_parser_ = use_parser; }

 private:
  enum class DecoderState {
    // The hardware's state doesn't reflect that of the H264MultiDecoderV1.
    kSwappedOut,

    kInitialWaitingForInput,
    kStoppedWaitingForInput,
    kWaitingForConfigChange,
    kRunning,
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
  void StartConfigChange();
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
  std::unique_ptr<media::DecoderBuffer> current_decoder_buffer_;

  std::optional<InternalBuffer> firmware_;
  std::optional<InternalBuffer> secondary_firmware_;
  std::optional<InternalBuffer> codec_data_;
  std::optional<InternalBuffer> aux_buf_;
  std::optional<InternalBuffer> lmem_;

  DecoderState state_ = DecoderState::kSwappedOut;

  uint32_t next_max_reference_size_ = 0u;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
  uint32_t mb_width_ = 0;
  uint32_t mb_height_ = 0;
  bool waiting_for_surfaces_ = false;
  bool currently_decoding_ = false;
  // This is true if media_decoder_ notified us about the config change, but the client hasn't yet
  // been requested to provide new frames.
  bool pending_config_change_ = false;
  bool in_pump_decoder_ = false;

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
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_V1_H_
