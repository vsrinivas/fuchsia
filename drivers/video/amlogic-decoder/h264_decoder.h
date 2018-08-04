// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_DECODER_H_

#include <ddk/io-buffer.h>

#include <vector>

#include "registers.h"
#include "video_decoder.h"

class H264Decoder : public VideoDecoder {
 public:
  H264Decoder(Owner* owner) : owner_(owner) {}

  ~H264Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  void SetFrameReadyNotifier(FrameReadyNotifier notifier) override;
  void SetInitializeFramesHandler(InitializeFramesHandler handler) override;
  // All H264Decoder errors require creating a new H264Decoder to recover.
  void SetErrorHandler(fit::closure error_handler) override;
  void ReturnFrame(std::shared_ptr<VideoFrame> frame) override;

 private:
  struct ReferenceFrame {
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;
  };

  zx_status_t ResetHardware();
  zx_status_t LoadSecondaryFirmware(const uint8_t* data,
                                    uint32_t firmware_size);
  zx_status_t InitializeFrames(uint32_t frame_count, uint32_t width,
                               uint32_t height, uint32_t display_width,
                               uint32_t display_height);
  zx_status_t InitializeStream();
  void ReceivedFrames(uint32_t frame_count);
  void SwitchStreams();
  void TryReturnFrames();
  void OnFatalError();

  Owner* owner_;
  io_buffer_t codec_data_ = {};
  io_buffer_t sei_data_buffer_ = {};
  io_buffer_t reference_mv_buffer_ = {};
  io_buffer_t secondary_firmware_ = {};
  // All H264Decoder errors require creating a new H264Decoder to recover.
  bool fatal_error_ = false;

  // TODO(dustingreen): Move these up to the VideoDecoder abstract base class.
  FrameReadyNotifier notifier_;
  InitializeFramesHandler initialize_frames_handler_;
  fit::closure error_handler_;

  std::vector<ReferenceFrame> video_frames_;
  std::vector<std::shared_ptr<VideoFrame>> returned_frames_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_H264_DECODER_H_
