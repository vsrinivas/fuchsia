// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AMLOGIC_VIDEO_H_
#define AMLOGIC_VIDEO_H_

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zx/handle.h>

#include <future>
#include <memory>
#include <thread>

#include "firmware_blob.h"
#include "registers.h"

class AmlogicVideo {
 public:
  enum class DeviceType {
    kUnknown,
    kGXM,   // S912
    kG12A,  // S905D2
  };

  ~AmlogicVideo();

  zx_status_t InitRegisters(zx_device_t* parent);
  zx_status_t InitDecoder();
  zx_status_t Bind();

  zx_status_t LoadDecoderFirmware(uint8_t* data, uint32_t size);

 private:
  friend class TestMpeg2;

  void EnableClockGate();
  void EnableVideoPower();
  zx_status_t InitializeStreamBuffer();
  zx_status_t InitializeEsParser();
  zx_status_t ParseVideo(void* data, uint32_t len);

  zx_device_t* parent_ = nullptr;
  zx_device_t* device_ = nullptr;
  platform_device_protocol_t pdev_;
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

  std::unique_ptr<FirmwareBlob> firmware_;

  // The stream buffer is a FIFO between the parser and the decoder.
  io_buffer_t stream_buffer_ = {};

  zx::handle bti_;

  std::promise<void> parser_finished_promise_;

  zx::handle parser_interrupt_handle_;
  zx::handle vdec1_interrupt_handle_;

  std::thread parser_interrupt_thread_;
};

#endif  // AMLOGIC_VIDEO_H_
