// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_AMLOGIC_VIDEO_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_AMLOGIC_VIDEO_H_

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/canvas.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zx/handle.h>

#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "decoder_core.h"
#include "device_ctx.h"
#include "firmware_blob.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "registers.h"
#include "stream_buffer.h"
#include "video_decoder.h"

class AmlogicVideo final : public VideoDecoder::Owner,
                           public DecoderCore::Owner {
 public:
  AmlogicVideo();

  ~AmlogicVideo();

  zx_status_t InitRegisters(zx_device_t* parent);
  zx_status_t InitDecoder();

  // VideoDecoder::Owner implementation.
  DosRegisterIo* dosbus() override { return dosbus_.get(); }
  zx_handle_t bti() override { return bti_.get(); }
  DeviceType device_type() override { return device_type_; }
  FirmwareBlob* firmware_blob() override { return firmware_.get(); }
  std::unique_ptr<CanvasEntry> ConfigureCanvas(io_buffer_t* io_buffer,
                                               uint32_t offset, uint32_t width,
                                               uint32_t height, uint32_t wrap,
                                               uint32_t blockmode) override;
  void FreeCanvas(std::unique_ptr<CanvasEntry> canvas) override;

  DecoderCore* core() override { return core_.get(); }
  zx_status_t AllocateIoBuffer(io_buffer_t* buffer, size_t size,
                               uint32_t alignment_log2,
                               uint32_t flags) override;
  PtsManager* pts_manager() override { return pts_manager_.get(); }

  // DecoderCore::Owner implementation.
  MmioRegisters* mmio() override { return registers_.get(); }
  void UngateClocks() override;
  void GateClocks() override;

 private:
  friend class TestH264;
  friend class TestMpeg2;
  friend class TestVP9;
  friend class CodecAdapterH264;

  zx_status_t InitializeStreamBuffer(bool use_parser, uint32_t size);
  zx_status_t InitializeEsParser();
  zx_status_t ParseVideo(void* data, uint32_t len);
  zx_status_t ProcessVideoNoParser(void* data, uint32_t len);
  zx_status_t ProcessVideoNoParserAtOffset(void* data, uint32_t len,
                                           uint32_t current_offset);
  void InitializeInterrupts();

  zx_device_t* parent_ = nullptr;
  platform_device_protocol_t pdev_;
  canvas_protocol_t canvas_;
  DeviceType device_type_ = DeviceType::kUnknown;
  io_buffer_t mmio_cbus_ = {};
  io_buffer_t mmio_dosbus_ = {};
  io_buffer_t mmio_hiubus_ = {};
  io_buffer_t mmio_aobus_ = {};
  io_buffer_t mmio_dmc_ = {};
  std::unique_ptr<CbusRegisterIo> cbus_;
  std::unique_ptr<DosRegisterIo> dosbus_;
  std::unique_ptr<HiuRegisterIo> hiubus_;
  std::unique_ptr<AoRegisterIo> aobus_;
  std::unique_ptr<DmcRegisterIo> dmc_;
  std::unique_ptr<ResetRegisterIo> reset_;
  std::unique_ptr<DemuxRegisterIo> demux_;
  std::unique_ptr<ParserRegisterIo> parser_;

  std::unique_ptr<MmioRegisters> registers_;

  std::unique_ptr<FirmwareBlob> firmware_;

  std::unique_ptr<StreamBuffer> stream_buffer_;

  // This buffer holds an ES start code that's used to get an interrupt when the
  // parser is finished.
  io_buffer_t search_pattern_ = {};

  zx::handle bti_;

  zx::event parser_finished_event_;

  zx::handle parser_interrupt_handle_;
  zx::handle vdec0_interrupt_handle_;
  zx::handle vdec1_interrupt_handle_;

  std::thread parser_interrupt_thread_;
  std::thread vdec0_interrupt_thread_;
  std::thread vdec1_interrupt_thread_;

  std::unique_ptr<DecoderCore> core_;

  std::unique_ptr<PtsManager> pts_manager_;
  std::mutex video_decoder_lock_;
  FXL_GUARDED_BY(video_decoder_lock_)
  std::unique_ptr<VideoDecoder> video_decoder_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_AMLOGIC_VIDEO_H_
