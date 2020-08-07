// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VIDEO_DECODER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VIDEO_DECODER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_adapter.h>
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

#include "amlogic_decoder_test_hooks.h"
#include "decoder_core.h"
#include "firmware_blob.h"
#include "macros.h"
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
  CanvasEntry(Owner* owner, uint32_t index) : owner_(owner), index_(index) {
    ZX_DEBUG_ASSERT(owner_);
  }

  ~CanvasEntry() { owner_->FreeCanvas(this); }

  __WARN_UNUSED_RESULT
  uint32_t index() const { return index_; }

 private:
  Owner* owner_{};
  uint32_t index_{};
};

class CodecPacket;
class DecoderInstance;
class Parser;
class Watchdog;

class VideoDecoder {
 public:
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
    [[nodiscard]] virtual zx_status_t TeeVp9AddHeaders(zx_paddr_t page_phys_base,
                                                       uint32_t before_size,
                                                       uint32_t max_after_size,
                                                       uint32_t* after_size) = 0;
    virtual __WARN_UNUSED_RESULT std::unique_ptr<CanvasEntry> ConfigureCanvas(
        io_buffer_t* io_buffer, uint32_t offset, uint32_t width, uint32_t height, uint32_t wrap,
        uint32_t blockmode) = 0;
    virtual __WARN_UNUSED_RESULT DecoderCore* core() = 0;
    [[nodiscard]] virtual DecoderCore* hevc_core() const = 0;
    [[nodiscard]] virtual DecoderCore* vdec1_core() const = 0;
    [[nodiscard]] virtual Parser* parser() {
      ZX_PANIC("not yet implemented by subclass");
      return nullptr;
    }
    [[nodiscard]] virtual DecoderInstance* current_instance() {
      ZX_PANIC("not yet implemented by subclass");
      return nullptr;
    }
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
    [[nodiscard]] virtual Watchdog* watchdog() = 0;
    [[nodiscard]] virtual zx_status_t ProcessVideoNoParser(const void* data, uint32_t len,
                                                           uint32_t* written_out = nullptr) {
      return ZX_OK;
    }
    [[nodiscard]] virtual uint32_t GetStreamBufferEmptySpace() {
      ZX_PANIC("not yet implemented by subclass");
      return 0;
    }
    [[nodiscard]] virtual uint32_t GetStreamBufferEmptySpaceAfterWriteOffsetBeforeReadOffset(
        uint32_t write_offset, uint32_t read_offset) {
      ZX_PANIC("not yet implemented by subclass");
      return 0;
    }
  };

  // The client of a video decoder is the component that receives (and allocates) output buffers.
  class Client {
   public:
    virtual void OnError() = 0;
    virtual void OnEos() = 0;
    virtual bool IsOutputReady() = 0;
    virtual void OnFrameReady(std::shared_ptr<VideoFrame> frame) = 0;
    virtual zx_status_t InitializeFrames(zx::bti, uint32_t min_frame_count,
                                         uint32_t max_frame_count, uint32_t width, uint32_t height,
                                         uint32_t stride, uint32_t display_width,
                                         uint32_t display_height, bool has_sar, uint32_t sar_width,
                                         uint32_t sar_height) = 0;
    virtual bool IsCurrentOutputBufferCollectionUsable(uint32_t min_frame_count,
                                                       uint32_t max_frame_count,
                                                       uint32_t coded_width, uint32_t coded_height,
                                                       uint32_t stride, uint32_t display_width,
                                                       uint32_t display_height) = 0;
    // Test hooks.
    virtual const AmlogicDecoderTestHooks& __WARN_UNUSED_RESULT test_hooks() const = 0;
  };

  VideoDecoder(Owner* owner, Client* client, bool is_secure);

  virtual __WARN_UNUSED_RESULT zx_status_t Initialize() = 0;
  virtual __WARN_UNUSED_RESULT zx_status_t InitializeHardware() { return ZX_ERR_NOT_SUPPORTED; }
  virtual void HandleInterrupt() = 0;
  virtual void CallErrorHandler() = 0;
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
  // h264_multi_decoder uses this to intentionally "swap out" without actually saving, to permit
  // restoring from a previously saved state, to re-try decode from the same input location again.
  // This is part of how stream style input is handled.
  virtual bool __WARN_UNUSED_RESULT MustBeSwappedOut() const { return false; }
  // h264_multi_decoder uses this to intentionally avoid saving when no useful progress was made, so
  // the decoder can re-feed the same input data again with more appended to the end.  This is part
  // of how stream style input is handled.
  virtual bool __WARN_UNUSED_RESULT ShouldSaveInputContext() const { return true; }
  virtual void OnSignaledWatchdog() {}
  // Initialize hardware protection.
  virtual zx_status_t SetupProtection() { return ZX_ERR_NOT_SUPPORTED; }

  virtual ~VideoDecoder() {}

  __WARN_UNUSED_RESULT PtsManager* pts_manager() { return pts_manager_.get(); }

  bool is_secure() const { return is_secure_; }

  const AmlogicDecoderTestHooks& __WARN_UNUSED_RESULT test_hooks() const {
    ZX_DEBUG_ASSERT(client_);
    return client_->test_hooks();
  }

  // for debug logging
  const uint32_t decoder_id_ = 0;

 protected:
  std::unique_ptr<PtsManager> pts_manager_;
  uint64_t next_non_codec_buffer_lifetime_ordinal_ = 0;
  Owner* owner_ = nullptr;
  Client* client_ = nullptr;
  bool is_secure_ = false;
};
#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VIDEO_DECODER_H_
