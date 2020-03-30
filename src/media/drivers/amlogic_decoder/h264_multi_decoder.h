// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_

#include <list>
#include <vector>

#include "macros.h"
#include "registers.h"
#include "video_decoder.h"

class MultiAccelerator;
namespace media {
class H264Decoder;
class H264DPB;
struct H264PPS;
struct H264SPS;
class DecoderBuffer;
}  // namespace media

// An H264 decoder that can be context-switched in and out.
class H264MultiDecoder : public VideoDecoder {
 public:
  struct ReferenceFrame {
    bool in_use = false;
    uint32_t index;
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;

    // TODO (use one per reference frame, rather than one per DPB frame)
    InternalBuffer reference_mv_buffer;
  };
  struct SliceData {};

  H264MultiDecoder(Owner* owner, Client* client);
  H264MultiDecoder(const H264MultiDecoder&) = delete;

  ~H264MultiDecoder() override;

  [[nodiscard]] zx_status_t Initialize() override;
  [[nodiscard]] zx_status_t InitializeHardware() override;
  void HandleInterrupt() override;
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;
  void CallErrorHandler() override;
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width, uint32_t height,
                         uint32_t stride) override;
  [[nodiscard]] bool CanBeSwappedIn() override;
  [[nodiscard]] bool CanBeSwappedOut() const override;
  void SetSwappedOut() override;
  void SwappedIn() override;

  zx_status_t InitializeBuffers();

  void ProcessNalUnit(std::vector<uint8_t> data);
  void FlushFrames();
  void DumpStatus();

  // For use by MultiAccelerator.
  void SubmitDataToHardware(const uint8_t* data, size_t length);
  void SubmitSliceData(const SliceData& data);
  void SubmitFrameMetadata(ReferenceFrame* reference_frame, const media::H264SPS* sps,
                           const media::H264PPS* pps, const media::H264DPB& dpb);
  void OutputFrame(ReferenceFrame* reference_frame);
  void StartFrameDecode();
  std::shared_ptr<ReferenceFrame> GetUnusedReferenceFrame();
  bool currently_decoding() { return currently_decoding_; }

 private:
  enum class DecoderState {
    // The hardware's state doesn't reflect that of the H264MultiDecoder.
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

  bool fatal_error_ = false;
  std::unique_ptr<media::H264Decoder> media_decoder_;
  std::list<std::unique_ptr<media::DecoderBuffer>> decoder_buffer_list_;
  std::unique_ptr<media::DecoderBuffer> current_decoder_buffer_;

  io_buffer_t secondary_firmware_;
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

  std::vector<std::shared_ptr<ReferenceFrame>> video_frames_;
  ReferenceFrame* current_frame_ = nullptr;
  ReferenceFrame* current_metadata_frame_ = nullptr;

  std::list<uint32_t> frames_to_output_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
