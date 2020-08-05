// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hevcdec.h"

#include <lib/trace/event.h>
#include <zircon/assert.h>

#include <algorithm>

#include <ddk/io-buffer.h>

#include "macros.h"
#include "memory_barriers.h"
#include "util.h"
#include "video_decoder.h"

static constexpr uint32_t kFirmwareSize = 4 * 4096;

std::optional<InternalBuffer> HevcDec::LoadFirmwareToBuffer(const uint8_t* data, uint32_t len) {
  TRACE_DURATION("media", "HevcDec::LoadFirmwareToBuffer");
  const uint32_t kBufferAlignShift = 16;
  auto create_result = InternalBuffer::CreateAligned(
      "Vdec1Firmware", &owner_->SysmemAllocatorSyncPtr(), owner_->bti(), kFirmwareSize,
      1 << kBufferAlignShift, /*is_secure=*/false, /*is_writable=*/true,
      /*is_mapping_needed=*/true);
  if (!create_result.is_ok()) {
    DECODE_ERROR("Failed to make firmware buffer - %d", create_result.error());
    return {};
  }
  auto buffer = create_result.take_value();
  memcpy(buffer.virt_base(), data, std::min(len, kFirmwareSize));
  buffer.CacheFlush(0, kFirmwareSize);
  BarrierAfterFlush();
  return std::move(buffer);
}

zx_status_t HevcDec::LoadFirmware(const uint8_t* data, uint32_t len) {
  auto buffer = LoadFirmwareToBuffer(data, len);
  if (!buffer)
    return ZX_ERR_NO_MEMORY;
  return LoadFirmware(*buffer);
}

zx_status_t HevcDec::LoadFirmware(InternalBuffer& buffer) {
  TRACE_DURATION("media", "HevcDec::LoadFirmware");
  ZX_DEBUG_ASSERT(buffer.size() == kFirmwareSize);
  HevcMpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  HevcCpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  HevcImemDmaAdr::Get().FromValue(truncate_to_32(buffer.phys_base())).WriteTo(mmio()->dosbus);
  HevcImemDmaCount::Get().FromValue(kFirmwareSize / sizeof(uint32_t)).WriteTo(mmio()->dosbus);
  HevcImemDmaCtrl::Get().FromValue(0x8000 | (7 << 16)).WriteTo(mmio()->dosbus);
  {
    TRACE_DURATION("media", "SpinWaitForRegister");

    // Measured spin wait time is around 5 microseconds on sherlock, so it makes sense to SpinWait.
    if (!SpinWaitForRegister(std::chrono::milliseconds(100), [this] {
          return (HevcImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000) == 0;
        })) {
      DECODE_ERROR("Failed to load microcode, ImemDmaCtrl %d, ImemDmaAdr 0x%x",
                   HevcImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value(),
                   HevcImemDmaAdr::Get().ReadFrom(mmio()->dosbus).reg_value());

      BarrierBeforeRelease();
      return ZX_ERR_TIMED_OUT;
    }
  }

  BarrierBeforeRelease();
  return ZX_OK;
}

void HevcDec::PowerOn() {
  ZX_DEBUG_ASSERT(!powered_on_);
  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  DosSwReset3::Get().FromValue(0xffffffff).WriteTo(mmio()->dosbus);
  DosSwReset3::Get().FromValue(0).WriteTo(mmio()->dosbus);

  owner_->UngateClocks();

  enum {
    kGxmFclkDiv4 = 0,  // 500 MHz
    kGxmFclkDiv3 = 1,  // 666 MHz
    kGxmFclkDiv5 = 2,  // 400 MHz
    kGxmFclkDiv7 = 3,  // 285.7 MHz
    kGxmMp1 = 4,
    kGxmMp2 = 5,
    kGxmGp0 = 6,
    kGxmXtal = 7,  // 24 MHz

    // G12B has the same clock inputs as G12A.
    kG12xFclkDiv2p5 = 0,  // 800 MHz
    kG12xFclkDiv3 = 1,    // 666 MHz
    kG12xFclkDiv4 = 2,    // 500 MHz
    kG12xFclkDiv5 = 3,    // 400 MHz
    kG12xFclkDiv7 = 4,    // 285.7 MHz
    kG12xHifi = 5,
    kG12xGp0 = 6,
    kG12xXtal = 7,  // 24 MHz
  };

  // Pick 500 MHz. The maximum frequency used in linux is 648 MHz, but that
  // requires using GP0, which is already being used by the GPU.
  // The linux driver also uses 200MHz in some circumstances for videos <=
  // 1080p30.
  uint32_t clock_sel =
      (owner_->device_type() == DeviceType::kG12A || owner_->device_type() == DeviceType::kG12B)
          ? kG12xFclkDiv4
          : kGxmFclkDiv4;

  auto clock_cntl = HhiHevcClkCntl::Get().FromValue(0).set_vdec_en(true).set_vdec_sel(clock_sel);
  // GXM HEVC doesn't have a front half.
  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
    clock_cntl.set_front_enable(true).set_front_sel(clock_sel);
  }
  clock_cntl.WriteTo(mmio()->hiubus);
  DosGclkEn3::Get().FromValue(0xffffffff).WriteTo(mmio()->dosbus);
  DosMemPdHevc::Get().FromValue(0).WriteTo(mmio()->dosbus);
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc00);
    temp.WriteTo(mmio()->aobus);
  }

  DosSwReset3::Get().FromValue(0xffffffff).WriteTo(mmio()->dosbus);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  DosSwReset3::Get().FromValue(0).WriteTo(mmio()->dosbus);
  powered_on_ = true;
}

void HevcDec::PowerOff() {
  ZX_DEBUG_ASSERT(powered_on_);
  powered_on_ = false;
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | 0xc00);
    temp.WriteTo(mmio()->aobus);
  }
  // Power down internal memory
  DosMemPdHevc::Get().FromValue(0xffffffffu).WriteTo(mmio()->dosbus);

  // Disable clocks
  HhiHevcClkCntl::Get()
      .FromValue(0)
      .set_vdec_en(false)
      .set_vdec_sel(3)
      .set_front_enable(false)
      .set_front_sel(3)
      .WriteTo(mmio()->hiubus);
  // Turn off power gates
  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | 0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  owner_->GateClocks();
}

void HevcDec::StartDecoding() {
  if (decoding_started_) {
    // idempotent if already started
    return;
  }
  ZX_DEBUG_ASSERT(!decoding_started_);
  decoding_started_ = true;
  // Delay to wait for previous command to finish.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset3::Get().ReadFrom(mmio()->dosbus);
  }

  DosSwReset3::Get().FromValue(0).set_mcpu(1).set_ccpu(1).WriteTo(mmio()->dosbus);
  DosSwReset3::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Delay to wait for previous command to finish.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset3::Get().ReadFrom(mmio()->dosbus);
  }

  HevcMpsr::Get().FromValue(1).WriteTo(mmio()->dosbus);
}

void HevcDec::StopDecoding() {
  if (!decoding_started_) {
    // idempotent if already stopped
    return;
  }
  ZX_DEBUG_ASSERT(decoding_started_);
  decoding_started_ = false;
  HevcMpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  HevcCpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);

  if (!WaitForRegister(std::chrono::seconds(1), [this] {
        return (HevcImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000) == 0;
      })) {
    DECODE_ERROR("Failed to wait for DMA completion");
    return;
  }
  // Delay to wait for previous command to finish.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset3::Get().ReadFrom(mmio()->dosbus);
  }
}

void HevcDec::WaitForIdle() {
  auto timeout = std::chrono::milliseconds(100);
  if (!WaitForRegister(timeout, [this] {
        return HevcMdecPicDcStatus::Get().ReadFrom(mmio()->dosbus).reg_value() == 0;
      })) {
    // Forcibly shutoff video output hardware. Probably.
    auto temp = HevcMdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(1 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~1 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      HevcMdecPicDcStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }
  if (!WaitForRegister(timeout, [this] {
        return !(HevcDblkStatus::Get().ReadFrom(mmio()->dosbus).reg_value() & 1);
      })) {
    // Forcibly shutoff deblocking hardware.
    HevcDblkCtrl::Get().FromValue(3).WriteTo(mmio()->dosbus);
    HevcDblkCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      HevcDblkStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }

  WaitForRegister(timeout, [this] {
    return !(HevcDcacDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000);
  });
}

void HevcDec::InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                                    uint32_t buffer_size) {
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_stream_fetch_enable(false)
      .WriteTo(mmio()->dosbus);
  HevcStreamStartAddr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  HevcStreamEndAddr::Get().FromValue(buffer_address + buffer_size).WriteTo(mmio()->dosbus);
  HevcStreamRdPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  HevcStreamWrPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
}

void HevcDec::InitializeParserInput() {
  DosGenCtrl0::Get().FromValue(0).set_vbuf_rp_select(DosGenCtrl0::kHevc).WriteTo(mmio()->dosbus);
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(true)
      .set_stream_fetch_enable(true)
      .WriteTo(mmio()->dosbus);
  HevcStreamFifoCtl::Get().ReadFrom(mmio()->dosbus).set_stream_fifo_hole(1).WriteTo(mmio()->dosbus);
}

void HevcDec::InitializeDirectInput() {
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(false)
      .set_stream_fetch_enable(false)
      .WriteTo(mmio()->dosbus);
  HevcStreamFifoCtl::Get().ReadFrom(mmio()->dosbus).set_stream_fifo_hole(1).WriteTo(mmio()->dosbus);
}

void HevcDec::UpdateWriteOffset(uint32_t write_offset) {
  uint32_t buffer_start = HevcStreamStartAddr::Get().ReadFrom(mmio()->dosbus).reg_value();
  UpdateWritePointer(buffer_start + write_offset);
}

void HevcDec::UpdateWritePointer(uint32_t write_pointer) {
  HevcStreamWrPtr::Get().FromValue(write_pointer).WriteTo(mmio()->dosbus);
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(false)
      .set_stream_fetch_enable(true)
      .WriteTo(mmio()->dosbus);
}

uint32_t HevcDec::GetStreamInputOffset() {
  uint32_t write_ptr = HevcStreamWrPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  uint32_t buffer_start = HevcStreamStartAddr::Get().ReadFrom(mmio()->dosbus).reg_value();
  ZX_ASSERT(write_ptr >= buffer_start);
  return write_ptr - buffer_start;
}

uint32_t HevcDec::GetReadOffset() {
  uint32_t read_ptr = HevcStreamRdPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  uint32_t buffer_start = HevcStreamStartAddr::Get().ReadFrom(mmio()->dosbus).reg_value();
  ZX_ASSERT(read_ptr >= buffer_start);
  return read_ptr - buffer_start;
}

zx_status_t HevcDec::InitializeInputContext(InputContext* context, bool is_secure) {
  constexpr uint32_t kInputContextSize = 4096;
  auto create_result = InternalBuffer::Create("HevcDecInputCtx", &owner_->SysmemAllocatorSyncPtr(),
                                              owner_->bti(), kInputContextSize, is_secure,
                                              /*is_writable=*/true, /*is_mapping_needed_=*/false);
  if (!create_result.is_ok()) {
    LOG(ERROR, "Failed to allocate input context - status: %d", create_result.error());
    return create_result.error();
  }
  // Sysmem has already written zeroes, flushed the zeroes, fenced the flush, to the extent
  // possible.
  context->buffer.emplace(create_result.take_value());
  return ZX_OK;
}

zx_status_t HevcDec::SaveInputContext(InputContext* context) {
  HevcStreamSwapAddr::Get()
      .FromValue(truncate_to_32(context->buffer->phys_base()))
      .WriteTo(mmio()->dosbus);
  HevcStreamSwapCtrl::Get().FromValue(0).set_enable(true).set_save(true).WriteTo(mmio()->dosbus);
  bool finished = SpinWaitForRegister(std::chrono::milliseconds(100), [this]() {
    return !HevcStreamSwapCtrl::Get().ReadFrom(mmio()->dosbus).in_progress();
  });
  if (!finished) {
    DECODE_ERROR("Timed out in HevcDec::SaveInputContext");
    return ZX_ERR_TIMED_OUT;
  }
  HevcStreamSwapCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);

  context->processed_video = HevcShiftByteCount::Get().ReadFrom(mmio()->dosbus).reg_value();
  return ZX_OK;
}

zx_status_t HevcDec::RestoreInputContext(InputContext* context) {
  // Stream fetching enabled needs to be set before the rest of the state is
  // restored, or else the parser's state becomes incorrect and decoding fails.
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(false)
      .set_stream_fetch_enable(true)
      .WriteTo(mmio()->dosbus);
  HevcStreamSwapAddr::Get()
      .FromValue(truncate_to_32(context->buffer->phys_base()))
      .WriteTo(mmio()->dosbus);
  HevcStreamSwapCtrl::Get().FromValue(0).set_enable(true).WriteTo(mmio()->dosbus);
  bool finished = SpinWaitForRegister(std::chrono::milliseconds(100), [this]() {
    return !HevcStreamSwapCtrl::Get().ReadFrom(mmio()->dosbus).in_progress();
  });
  if (!finished) {
    DECODE_ERROR("Timed out in HevcDec::RestoreInputContext");
    return ZX_ERR_TIMED_OUT;
  }
  HevcStreamSwapCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  return ZX_OK;
}
