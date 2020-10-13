// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_VIDEO_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_VIDEO_H_

#include <fuchsia/tee/cpp/fidl.h>
#include <lib/zx/handle.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/clock.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddk/protocol/tee.h>
#include <tee-client-api/tee-client-types.h>

#include "decoder_core.h"
#include "decoder_instance.h"
#include "device_ctx.h"
#include "firmware_blob.h"
#include "parser.h"
#include "registers.h"
#include "secmem_session.h"
#include "stream_buffer.h"
#include "video_decoder.h"
#include "watchdog.h"

class AmlogicVideo final : public VideoDecoder::Owner,
                           public DecoderCore::Owner,
                           public CanvasEntry::Owner,
                           public Parser::Owner,
                           public Watchdog::Owner {
 public:
  AmlogicVideo();

  ~AmlogicVideo();

  void SetMetrics(CodecMetrics* metrics);
  [[nodiscard]] zx_status_t InitRegisters(zx_device_t* parent);
  [[nodiscard]] zx_status_t InitDecoder();

  // VideoDecoder::Owner implementation.
  [[nodiscard]] CodecMetrics& metrics() override {
    ZX_DEBUG_ASSERT(metrics_);
    return *metrics_;
  }
  [[nodiscard]] DosRegisterIo* dosbus() override { return dosbus_.get(); }
  [[nodiscard]] zx::unowned_bti bti() override { return zx::unowned_bti(bti_); }
  [[nodiscard]] DeviceType device_type() override { return device_type_; }
  [[nodiscard]] FirmwareBlob* firmware_blob() override { return firmware_.get(); }
  [[nodiscard]] bool is_tee_available() override { return is_tee_available_; }
  [[nodiscard]] zx_status_t TeeSmcLoadVideoFirmware(
      FirmwareBlob::FirmwareType index, FirmwareBlob::FirmwareVdecLoadMode vdec) override;
  [[nodiscard]] zx_status_t TeeVp9AddHeaders(zx_paddr_t page_phys_base, uint32_t before_size,
                                             uint32_t max_after_size,
                                             uint32_t* after_size) override;
  [[nodiscard]] std::unique_ptr<CanvasEntry> ConfigureCanvas(io_buffer_t* io_buffer,
                                                             uint32_t offset, uint32_t width,
                                                             uint32_t height, uint32_t wrap,
                                                             uint32_t blockmode) override;

  [[nodiscard]] DecoderCore* core() override { return core_; }
  [[nodiscard]] zx_status_t AllocateIoBuffer(io_buffer_t* buffer, size_t size,
                                             uint32_t alignment_log2, uint32_t flags,
                                             const char* name) override;
  [[nodiscard]] fuchsia::sysmem::AllocatorSyncPtr& SysmemAllocatorSyncPtr() override;

  [[nodiscard]] bool IsDecoderCurrent(VideoDecoder* decoder) override {
    AssertVideoDecoderLockHeld();
    assert(decoder);
    return decoder == video_decoder_;
  }
  [[nodiscard]] zx_status_t SetProtected(ProtectableHardwareUnit unit, bool protect) override;
  // This tries to schedule the next runnable decoder. It may leave the current
  // decoder scheduled if no other decoder is runnable.
  void TryToReschedule() override __TA_REQUIRES(video_decoder_lock_);
  [[nodiscard]] Watchdog* watchdog() override __TA_REQUIRES(video_decoder_lock_) {
    return &watchdog_;
  }

  // DecoderCore::Owner implementation.
  [[nodiscard]] MmioRegisters* mmio() override { return registers_.get(); }
  void UngateClocks() override;
  void GateClocks() override;
  void ToggleClock(ClockType type, bool enable) override;

  // CanvasEntry::Owner implementation.
  void FreeCanvas(CanvasEntry* canvas) override;

  // Parser::Owner implementation.
  [[nodiscard]] bool is_parser_gated() const override { return is_parser_gated_; }

  // Watchdog::Owner implementation.
  void OnSignaledWatchdog() override;

  // The pts manager has its own locking, so don't worry about the video decoder
  // lock.
  [[nodiscard]] PtsManager* pts_manager() __TA_NO_THREAD_SAFETY_ANALYSIS {
    ZX_DEBUG_ASSERT(video_decoder_);
    return video_decoder_->pts_manager();
  }

  // Reset the current instance - only for use with single-stream decoders.
  void ClearDecoderInstance();

  // Erase a specific decoder. May switch to a different decoder in multi-stream
  // mode. This will stop and power off the core if the decoder is currently
  // running.
  void RemoveDecoder(VideoDecoder* decoder);
  void RemoveDecoderLocked(VideoDecoder* decoder) __TA_REQUIRES(video_decoder_lock_);

  [[nodiscard]] zx_status_t InitializeStreamBuffer(bool use_parser, uint32_t size, bool is_secure);
  [[nodiscard]] zx_status_t InitializeEsParser();

  [[nodiscard]] Parser* parser() override { return parser_.get(); }

  void UngateParserClock();
  void GateParserClock();

  [[nodiscard]] zx_status_t ProcessVideoNoParser(const void* data, uint32_t len,
                                                 uint32_t* written_out = nullptr) override;

  [[nodiscard]] uint32_t GetStreamBufferEmptySpaceAfterWriteOffsetBeforeReadOffset(
      uint32_t write_offset, uint32_t read_offset) override;

  [[nodiscard]] uint32_t GetStreamBufferEmptySpaceAfterOffset(uint32_t write_offset);

  // Similar to GetStreamBufferEmptySpaceAfterOffset, but uses the current core write offset.
  [[nodiscard]] uint32_t GetStreamBufferEmptySpace() override;

  [[nodiscard]] DecoderCore* hevc_core() const override { return hevc_core_.get(); }
  [[nodiscard]] DecoderCore* vdec1_core() const override { return vdec1_core_.get(); }

  // Add the instance as a swapped-out decoder.
  void AddNewDecoderInstance(std::unique_ptr<DecoderInstance> instance)
      __TA_REQUIRES(video_decoder_lock_);

  // For single-instance decoders, set the default instance.
  void SetDefaultInstance(std::unique_ptr<VideoDecoder> decoder, bool hevc)
      __TA_REQUIRES(video_decoder_lock_);
  [[nodiscard]] std::mutex* video_decoder_lock() __TA_RETURN_CAPABILITY(video_decoder_lock_) {
    return &video_decoder_lock_;
  }
  [[nodiscard]] VideoDecoder* video_decoder() __TA_REQUIRES(video_decoder_lock_) {
    return video_decoder_;
  }
  [[nodiscard]] DecoderInstance* current_instance() override __TA_REQUIRES(video_decoder_lock_) {
    return current_instance_.get();
  }

  // This should be called only to mollify the lock detection in cases where
  // it's guaranteed that the video decoder lock is already held. This can't
  // actually be implemented on top of std::mutex.
  void AssertVideoDecoderLockHeld() __TA_ASSERT(video_decoder_lock_) {}

  [[nodiscard]] zx_status_t AllocateStreamBuffer(StreamBuffer* buffer, uint32_t size,
                                                 bool use_parser, bool is_secure);

  // This gets started connecting to sysmem, but returns an InterfaceHandle
  // instead of InterfacePtr so that the caller can bind to the dispatcher.
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> ConnectToSysmem();

 private:
  friend class TestH264;
  friend class TestMpeg2;
  friend class TestVP9;
  friend class TestFrameProvider;

  zx_status_t ConnectToTee(fuchsia::tee::DeviceSyncPtr* tee);

  zx_status_t EnsureSecmemSessionIsConnected();

  void InitializeStreamInput(bool use_parser);

  [[nodiscard]] zx_status_t ProcessVideoNoParserAtOffset(const void* data, uint32_t len,
                                                         uint32_t current_offset,
                                                         uint32_t* written_out = nullptr);

  zx_status_t PreloadFirmwareViaTee();
  void InitializeInterrupts();
  void SwapOutCurrentInstance() __TA_REQUIRES(video_decoder_lock_);
  void SwapInCurrentInstance() __TA_REQUIRES(video_decoder_lock_);
  // Signals the current decoder that there's an error and tells it to power off.
  void PowerOffForError() __TA_REQUIRES(video_decoder_lock_);

  zx_device_t* parent_ = nullptr;
  pdev_protocol_t pdev_{};
  sysmem_protocol_t sysmem_{};
  amlogic_canvas_protocol_t canvas_{};

  clock_protocol_t clocks_[static_cast<int>(ClockType::kMax)] = {};

  // Unlike sysmem and canvas, tee is optional (no tee on vim2).
  tee_protocol_t tee_{};
  bool is_tee_available_ = false;
  std::optional<SecmemSession> secmem_session_;

  CodecMetrics default_nop_metrics_;
  CodecMetrics* metrics_ = &default_nop_metrics_;

  DeviceType device_type_ = DeviceType::kUnknown;
  zx::handle secure_monitor_;
  std::unique_ptr<CbusRegisterIo> cbus_;
  std::unique_ptr<DosRegisterIo> dosbus_;
  std::unique_ptr<HiuRegisterIo> hiubus_;
  std::unique_ptr<AoRegisterIo> aobus_;
  std::unique_ptr<DmcRegisterIo> dmc_;
  std::unique_ptr<ResetRegisterIo> reset_;
  std::unique_ptr<DemuxRegisterIo> demux_;
  std::unique_ptr<ParserRegisterIo> parser_regs_;

  std::unique_ptr<MmioRegisters> registers_;

  std::unique_ptr<FirmwareBlob> firmware_;

  // Private for use by AmlogicVideo, when creating InternalBuffer(s).  Decoders
  // can create their own separate InterfaceHandle<Allocator>(s) by calling
  // ConnectToSysmem().
  fuchsia::sysmem::AllocatorSyncPtr sysmem_sync_ptr_;

  zx::bti bti_;

  zx::handle parser_interrupt_handle_;
  zx::handle vdec0_interrupt_handle_;
  zx::handle vdec1_interrupt_handle_;

  std::thread parser_interrupt_thread_;
  std::thread vdec0_interrupt_thread_;
  std::thread vdec1_interrupt_thread_;

  std::unique_ptr<DecoderCore> hevc_core_;
  std::unique_ptr<DecoderCore> vdec1_core_;

  std::mutex video_decoder_lock_;
  // This is the video decoder that's currently attached to the hardware.
  __TA_GUARDED(video_decoder_lock_)
  VideoDecoder* video_decoder_ = nullptr;

  // This is the stream buffer that's currently attached to the hardware.
  StreamBuffer* stream_buffer_ = nullptr;

  // The decoder core for the currently-running decoder. It must be powered on.
  DecoderCore* core_ = nullptr;

  std::unique_ptr<Parser> parser_;
  bool is_parser_gated_ = true;

  __TA_GUARDED(video_decoder_lock_)
  std::unique_ptr<DecoderInstance> current_instance_;
  __TA_GUARDED(video_decoder_lock_)
  std::list<std::unique_ptr<DecoderInstance>> swapped_out_instances_;

  // This is the watchdog for the video decoder core. It's owned by AmlogicVideo and not the video
  // decoder because destroying it can (implicitly) block on the video_decoder_lock_, while decoder
  // destruction happens with the video decoder lock held.
  __TA_GUARDED(video_decoder_lock_)
  Watchdog watchdog_{this};
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_VIDEO_H_
