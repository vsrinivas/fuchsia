// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_DECODER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_frame.h>
#include <lib/zx/bti.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <functional>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "decoder_core.h"
#include "firmware_blob.h"
#include "pts_manager.h"
#include "registers.h"
#include "video_frame.h"

class FirmwareBlob;
class PtsManager;

enum class DeviceType {
  kUnknown,
  // These should be ordered from oldest to newest.
  kGXM = 1,   // S912
  kG12A = 2,  // S905D2
  kG12B = 3,  // T931
};

// Returns true if |a| is newer than or the same as |b|.
inline bool IsDeviceAtLeast(DeviceType a, DeviceType b) {
  return static_cast<int>(a) >= static_cast<int>(b);
}

class CanvasEntry {
 public:
  class Owner {
   public:
    virtual void FreeCanvas(CanvasEntry* canvas) = 0;
  };
  CanvasEntry(Owner* owner, uint32_t index) : owner_(owner), index_(index) {}

  ~CanvasEntry() { owner_->FreeCanvas(this); }

  __WARN_UNUSED_RESULT
  uint32_t index() const { return index_; }

 private:
  Owner* owner_;
  uint32_t index_;
};

class CodecPacket;
class VideoDecoder {
 public:
  using IsCurrentOutputBufferCollectionUsable =
      fit::function<bool(uint32_t frame_count, uint32_t coded_width, uint32_t coded_height,
                         uint32_t stride, uint32_t display_width, uint32_t display_height)>;
  using InitializeFramesHandler = fit::function<zx_status_t(::zx::bti,
                                                            uint32_t,  // frame_count
                                                            uint32_t,  // width
                                                            uint32_t,  // height
                                                            uint32_t,  // stride
                                                            uint32_t,  // display_width
                                                            uint32_t,  // display_height
                                                            bool,      // has_sar
                                                            uint32_t,  // sar_width
                                                            uint32_t   // sar_height
                                                            )>;
  // In actual operation, the FrameReadyNotifier must not keep a reference on
  // the frame shared_ptr<>, as that would interfere with muting calls to
  // ReturnFrame().  See comment on Vp9Decoder::Frame::frame field.
  using FrameReadyNotifier = fit::function<void(std::shared_ptr<VideoFrame>)>;
  using EosHandler = fit::closure;
  using CheckOutputReady = fit::function<bool()>;
  class Owner {
   public:
    enum class ProtectableHardwareUnit {
      // From BL32.
      kHevc = 4,
      kParser = 7,
      kVdec = 13
    };

    virtual __WARN_UNUSED_RESULT DosRegisterIo* dosbus() = 0;
    virtual __WARN_UNUSED_RESULT zx::unowned_bti bti() = 0;
    virtual __WARN_UNUSED_RESULT DeviceType device_type() = 0;
    virtual __WARN_UNUSED_RESULT FirmwareBlob* firmware_blob() = 0;
    [[nodiscard]] virtual bool is_tee_available() = 0;
    // Requires is_tee_available() true.
    [[nodiscard]] virtual zx_status_t TeeSmcLoadVideoFirmware(
        FirmwareBlob::FirmwareType index, FirmwareBlob::FirmwareVdecLoadMode vdec) = 0;
    virtual __WARN_UNUSED_RESULT std::unique_ptr<CanvasEntry> ConfigureCanvas(
        io_buffer_t* io_buffer, uint32_t offset, uint32_t width, uint32_t height, uint32_t wrap,
        uint32_t blockmode) = 0;
    virtual __WARN_UNUSED_RESULT DecoderCore* core() = 0;
    virtual __WARN_UNUSED_RESULT zx_status_t AllocateIoBuffer(io_buffer_t* buffer, size_t size,
                                                              uint32_t alignment_log2,
                                                              uint32_t flags, const char* name) = 0;
    [[nodiscard]] virtual fuchsia::sysmem::AllocatorSyncPtr& SysmemAllocatorSyncPtr() = 0;

    virtual __WARN_UNUSED_RESULT bool IsDecoderCurrent(VideoDecoder* decoder) = 0;
    // Sets whether a particular hardware unit can read/write protected or
    // unprotected memory.
    virtual __WARN_UNUSED_RESULT zx_status_t SetProtected(ProtectableHardwareUnit unit,
                                                          bool protect) = 0;
    // Signal that the scheduler should try scheduling a new decoder, either because the current
    // decoder finished a frame or because a new decoder is now runnable.  Must be called with the
    // decoder lock held.
    virtual void TryToReschedule() = 0;
  };

  explicit VideoDecoder(Owner* owner, bool is_secure) : owner_(owner), is_secure_(is_secure) {
    pts_manager_ = std::make_unique<PtsManager>();
  }

  virtual __WARN_UNUSED_RESULT zx_status_t Initialize() = 0;
  virtual __WARN_UNUSED_RESULT zx_status_t InitializeHardware() { return ZX_ERR_NOT_SUPPORTED; }
  virtual void HandleInterrupt() = 0;
  virtual void SetIsCurrentOutputBufferCollectionUsable(
      IsCurrentOutputBufferCollectionUsable is_current_output_buffer_collection_usable) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  }
  virtual void SetInitializeFramesHandler(InitializeFramesHandler handler) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  }
  virtual void SetFrameReadyNotifier(FrameReadyNotifier notifier) = 0;
  virtual void SetEosHandler(EosHandler eos_handler) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  }
  virtual void SetErrorHandler(fit::closure error_handler) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  }
  virtual void CallErrorHandler() = 0;
  virtual void SetCheckOutputReady(CheckOutputReady checkOutputReady) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  };
  virtual void ReturnFrame(std::shared_ptr<VideoFrame> frame) = 0;
  virtual void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width, uint32_t height,
                                 uint32_t stride) = 0;
  virtual void SetSwappedOut() {}
  virtual void SwappedIn() {}
  // Returns true if the instance has more data to decode and output buffers to
  // decode it into.
  virtual bool __WARN_UNUSED_RESULT CanBeSwappedIn() { return false; }
  // Returns true if the decoder is at a place where it can be swapped out.
  virtual bool __WARN_UNUSED_RESULT CanBeSwappedOut() const { return false; }
  virtual ~VideoDecoder() {}

  __WARN_UNUSED_RESULT PtsManager* pts_manager() { return pts_manager_.get(); }

  bool is_secure() const { return is_secure_; }

 protected:
  std::unique_ptr<PtsManager> pts_manager_;
  uint64_t next_non_codec_buffer_lifetime_ordinal_ = 0;
  Owner* owner_ = nullptr;
  bool is_secure_ = false;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_DECODER_H_
