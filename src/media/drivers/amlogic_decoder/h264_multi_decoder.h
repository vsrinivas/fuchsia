// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_MULTI_DECODER_H_

#include <vector>

#include "macros.h"
#include "registers.h"
#include "video_decoder.h"

// An H264 decoder that can be context-switched in and out.
class H264MultiDecoder : public VideoDecoder {
 public:
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

  void UpdateDecodeSize();

 private:
  struct ReferenceFrame {
    uint32_t index;
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;

    // TODO (use one per reference frame, rather than one per DPB frame)
    InternalBuffer reference_mv_buffer;
  };

  zx_status_t LoadSecondaryFirmware(const uint8_t* data, uint32_t firmware_size);
  void ResetHardware();
  void ConfigureDpb();
  void HandleSliceHeadDone();
  void HandlePicDataDone();
  void OnFatalError();

  bool fatal_error_ = false;

  io_buffer_t secondary_firmware_;
  std::optional<InternalBuffer> codec_data_;
  std::optional<InternalBuffer> aux_buf_;
  std::optional<InternalBuffer> lmem_;

  uint32_t next_max_reference_size_ = 0u;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
  uint32_t mb_width_ = 0;
  uint32_t mb_height_ = 0;

  std::vector<ReferenceFrame> video_frames_;
  ReferenceFrame* current_frame_ = nullptr;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_MULTI_DECODER_H_
