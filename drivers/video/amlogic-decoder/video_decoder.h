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

#include "decoder_core.h"
#include "registers.h"
#include "video_frame.h"

class FirmwareBlob;

enum class DeviceType {
  kUnknown,
  kGXM,   // S912
  kG12A,  // S905D2
};

class CanvasEntry {
 public:
  CanvasEntry(uint32_t index) : index_(index) {}

  ~CanvasEntry() { assert(!valid_); }

  uint32_t index() const {
    assert(valid_);
    return index_;
  }
  void invalidate() {
    assert(valid_);
    valid_ = false;
  }

 private:
  uint32_t index_;
  bool valid_ = true;
};

class VideoDecoder {
 public:
  using FrameReadyNotifier = std::function<void(std::shared_ptr<VideoFrame>)>;
  class Owner {
   public:
    virtual DosRegisterIo* dosbus() = 0;
    virtual zx_handle_t bti() = 0;
    virtual DeviceType device_type() = 0;
    virtual FirmwareBlob* firmware_blob() = 0;
    virtual std::unique_ptr<CanvasEntry> ConfigureCanvas(
        io_buffer_t* io_buffer, uint32_t offset, uint32_t width,
        uint32_t height, uint32_t wrap, uint32_t blockmode) = 0;
    virtual void FreeCanvas(std::unique_ptr<CanvasEntry> canvas) = 0;
    virtual DecoderCore* core() = 0;
  };

  virtual zx_status_t Initialize() = 0;
  virtual void HandleInterrupt() = 0;
  virtual void SetFrameReadyNotifier(FrameReadyNotifier notifier) {}
  virtual void ReturnFrame(std::shared_ptr<VideoFrame> frame) = 0;
  virtual ~VideoDecoder() {}
};

#endif  // VIDEO_DECODER_H_
