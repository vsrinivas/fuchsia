// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MPEG12_DECODER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MPEG12_DECODER_H_

#include <lib/device-protocol/platform-device.h>

#include <thread>
#include <vector>

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

#include "registers.h"
#include "video_decoder.h"

class Mpeg12Decoder : public VideoDecoder {
 public:
  explicit Mpeg12Decoder(Owner* owner, Client* client)
      : VideoDecoder(
            media_metrics::StreamProcessorEventsMetricDimensionImplementation_AmlogicMpeg12, owner,
            client,
            /*is_secure=*/false) {
    power_ref_ = std::make_unique<PowerReference>(owner_->core());
  }

  ~Mpeg12Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  void ReturnFrame(std::shared_ptr<VideoFrame> video_frame) override;
  void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width, uint32_t height,
                         uint32_t stride) override;
  void CallErrorHandler() override { exit(-1); }

 private:
  struct ReferenceFrame {
    std::shared_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;
  };
  zx_status_t InitializeVideoBuffers();
  void ResetHardware();
  void TryReturnFrames();
  std::unique_ptr<PowerReference> power_ref_;

  std::vector<ReferenceFrame> video_frames_;
  std::vector<std::shared_ptr<VideoFrame>> returned_frames_;
  io_buffer_t workspace_buffer_ = {};
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MPEG12_DECODER_H_
