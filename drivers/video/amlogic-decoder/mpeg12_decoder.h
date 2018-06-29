// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MPEG12_DECODER_H_
#define MPEG12_DECODER_H_

#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <thread>
#include <vector>

#include "registers.h"
#include "video_decoder.h"

class Mpeg12Decoder : public VideoDecoder {
 public:
  Mpeg12Decoder(Owner* owner) : owner_(owner) {}

  ~Mpeg12Decoder() override;

  zx_status_t Initialize() override;
  void HandleInterrupt() override;
  void SetFrameReadyNotifier(FrameReadyNotifier notifier) override;

 private:
  struct ReferenceFrame {
    std::unique_ptr<VideoFrame> frame;
    std::unique_ptr<CanvasEntry> y_canvas;
    std::unique_ptr<CanvasEntry> uv_canvas;
  };
  zx_status_t InitializeVideoBuffers();
  void ResetHardware();

  Owner* owner_;

  FrameReadyNotifier notifier_;
  std::vector<ReferenceFrame> video_frames_;
  io_buffer_t workspace_buffer_ = {};
};

#endif  // MPEG12_DECODER_H_
