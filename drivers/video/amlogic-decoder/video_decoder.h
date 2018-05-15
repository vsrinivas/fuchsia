// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIDEO_DECODER_H_
#define VIDEO_DECODER_H_

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <functional>

#include "registers.h"

class FirmwareBlob;

struct VideoFrame {
  ~VideoFrame() { io_buffer_release(&buffer); }

  io_buffer_t buffer = {};
  uint32_t stride = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t uv_plane_offset = 0;
};

class VideoDecoder {
 public:
  using FrameReadyNotifier = std::function<void(VideoFrame*)>;
  class Owner {
   public:
    virtual DosRegisterIo* dosbus() = 0;
    virtual zx_handle_t bti() = 0;
    virtual FirmwareBlob* firmware_blob() = 0;
    virtual zx_status_t LoadDecoderFirmware(uint8_t* data, uint32_t size) = 0;
    virtual zx_status_t ConfigureCanvas(uint32_t id, uint32_t addr,
                                        uint32_t width, uint32_t height,
                                        uint32_t wrap, uint32_t blockmode) = 0;
    virtual void StartDecoding() = 0;
    virtual void StopDecoding() = 0;
    virtual void PowerDownDecoder() = 0;
  };

  virtual zx_status_t Initialize() = 0;
  virtual void HandleInterrupt() = 0;
  virtual void SetFrameReadyNotifier(FrameReadyNotifier notifier) {}
  virtual ~VideoDecoder() {}
};

#endif  // VIDEO_DECODER_H_
