// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_DECODER_H_

#include <vector>

#include <ddk/io-buffer.h>

#include "internal_buffer.h"
#include "registers.h"
#include "video_decoder.h"

class H264Decoder : public VideoDecoder {
 public:
  // This is the state of the actual firmware.
  enum class DecoderState {
    // Decoder is in a state ready to decode new frames.
    kRunning,

    // Decoder is paused waiting for reference frame canvases to be initialized.
    kWaitingForNewFrames,
  };

  H264Decoder(Owner* owner, Client* client, bool is_secure)
      : VideoDecoder(owner, client, is_secure) {}

  ~H264Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  // All H264Decoder errors require creating a new H264Decoder to recover.
  void CallErrorHandler() override { client_->OnError(); }
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width, uint32_t height,
                         uint32_t stride) override;

 private:
  struct ReferenceFrame {
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;
  };

  zx_status_t ResetHardware();
  zx_status_t LoadSecondaryFirmware(const uint8_t* data, uint32_t firmware_size);
  zx_status_t InitializeFrames(uint32_t min_frame_count, uint32_t max_frame_count, uint32_t width,
                               uint32_t height, uint32_t display_width, uint32_t display_height,
                               bool has_sar, uint32_t sar_width, uint32_t sar_height);
  zx_status_t InitializeStream();
  void ReceivedFrames(uint32_t frame_count);
  void SwitchStreams();
  void TryReturnFrames();
  void OnFatalError();

  std::optional<InternalBuffer> codec_data_;
  std::optional<InternalBuffer> sei_data_buffer_;
  std::optional<InternalBuffer> reference_mv_buffer_;
  io_buffer_t secondary_firmware_ = {};
  // All H264Decoder errors require creating a new H264Decoder to recover.
  bool fatal_error_ = false;
  DecoderState state_ = DecoderState::kRunning;

  // These are set in InitializeFrames for use in InitializedFrames.
  // next_mv_buffer_count_ and next_max_dpb_size_ are specified to the firmware, along with the
  // actual number of frames.  It's not immediately clear why/whether the firmware actually needs
  // these in addition to actual number of frames.
  uint32_t next_mv_buffer_count_ = 0;
  uint32_t next_max_dpb_size_ = 0;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  std::vector<ReferenceFrame> video_frames_;
  std::vector<std::shared_ptr<VideoFrame>> returned_frames_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_DECODER_H_
