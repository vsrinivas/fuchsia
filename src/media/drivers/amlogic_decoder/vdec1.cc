// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vdec1.h"

#include <algorithm>

#include <ddk/io-buffer.h>

#include "macros.h"
#include "memory_barriers.h"
#include "registers.h"
#include "util.h"
#include "video_decoder.h"

zx_status_t Vdec1::LoadFirmware(const uint8_t* data, uint32_t size) {
  Mpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  Cpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  io_buffer_t firmware_buffer;
  const uint32_t kFirmwareSize = 4 * 4096;
  // Most buffers should be 64-kbyte aligned.
  const uint32_t kBufferAlignShift = 16;
  zx_status_t status = io_buffer_init_aligned(&firmware_buffer, owner_->bti()->get(), kFirmwareSize,
                                              kBufferAlignShift, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to make firmware buffer");
    return status;
  }
  SetIoBufferName(&firmware_buffer, "Vdec1Firmware");

  memcpy(io_buffer_virt(&firmware_buffer), data, std::min(size, kFirmwareSize));
  io_buffer_cache_flush(&firmware_buffer, 0, kFirmwareSize);

  BarrierAfterFlush();
  ImemDmaAdr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&firmware_buffer)))
      .WriteTo(mmio()->dosbus);
  ImemDmaCount::Get().FromValue(kFirmwareSize / sizeof(uint32_t)).WriteTo(mmio()->dosbus);
  ImemDmaCtrl::Get().FromValue(0x8000 | (7 << 16)).WriteTo(mmio()->dosbus);

  if (!WaitForRegister(std::chrono::milliseconds(100), [this] {
        return (ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000) == 0;
      })) {
    DECODE_ERROR("Failed to load microcode, ImemDmaCtrl %d, ImemDmaAdr 0x%x",
                 ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value(),
                 ImemDmaAdr::Get().ReadFrom(mmio()->dosbus).reg_value());

    BarrierBeforeRelease();
    io_buffer_release(&firmware_buffer);
    return ZX_ERR_TIMED_OUT;
  }

  BarrierBeforeRelease();
  io_buffer_release(&firmware_buffer);
  return ZX_OK;
}

void Vdec1::PowerOn() {
  if (powered_on_) {
    return;
  }

  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc);
    temp.WriteTo(mmio()->aobus);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  DosSwReset0::Get().FromValue(0xfffffffc).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

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

  HhiVdecClkCntl::Get()
      .ReadFrom(mmio()->hiubus)
      .set_vdec_en(true)
      .set_vdec_sel(clock_sel)
      .WriteTo(mmio()->hiubus);
  owner_->ToggleClock(ClockType::kGclkVdec, true);

  DosMemPdVdec::Get().FromValue(0).WriteTo(mmio()->dosbus);
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  DosVdecMcrccStallCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_vdec(true).WriteTo(mmio()->dmc);

  MdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus).set_bit31(false).WriteTo(mmio()->dosbus);

  // Reset all the hardware again. Doing it at this time doesn't match the linux driver, but instead
  // matches the hardware documentation. If we don't do this, restoring the input context or loading
  // the firmware can hang.
  DosSwReset0::Get().FromValue(0xfffffffc).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);
  powered_on_ = true;
}

void Vdec1::PowerOff() {
  if (!powered_on_)
    return;
  powered_on_ = false;
  DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_vdec(false).WriteTo(mmio()->dmc);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | 0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  DosMemPdVdec::Get().FromValue(~0u).WriteTo(mmio()->dosbus);
  HhiVdecClkCntl::Get().ReadFrom(mmio()->hiubus).set_vdec_en(false).WriteTo(mmio()->hiubus);

  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | 0xc);
    temp.WriteTo(mmio()->aobus);
  }
  owner_->GateClocks();
}

void Vdec1::StartDecoding() {
  // Delay to ensure previous writes have executed.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(mmio()->dosbus);
  }

  DosSwReset0::Get().FromValue(0).set_vdec_ccpu(1).set_vdec_mcpu(1).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Delay to ensure previous writes have executed.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(mmio()->dosbus);
  }

  Mpsr::Get().FromValue(1).WriteTo(mmio()->dosbus);
  decoding_started_ = true;
}

void Vdec1::StopDecoding() {
  if (!decoding_started_)
    return;
  decoding_started_ = false;
  Mpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  Cpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);

  if (!WaitForRegister(std::chrono::milliseconds(100), [this] {
        return (ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000) == 0;
      })) {
    DECODE_ERROR("Failed to wait for DMA completion");
    return;
  }
  // Delay to ensure previous writes have executed.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(mmio()->dosbus);
  }

  DosSwReset0::Get().FromValue((1 << 12) | (1 << 11)).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Delay to ensure previous write have executed.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(mmio()->dosbus);
  }
}

void Vdec1::WaitForIdle() {
  auto timeout = std::chrono::milliseconds(100);
  LOG(TRACE, "MdecPicDcStatus wait...");
  if (!WaitForRegister(timeout, [this] {
        return MdecPicDcStatus::Get().ReadFrom(mmio()->dosbus).reg_value() == 0;
      })) {
    // Forcibly shutoff video output hardware. Probably.
    LOG(TRACE, "Forcibly MdecPicDcCtrl...");
    auto temp = MdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(1 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~1 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      MdecPicDcStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }
  LOG(TRACE, "DblkStatus wait...");
  if (!WaitForRegister(timeout, [this] {
        return !(DblkStatus::Get().ReadFrom(mmio()->dosbus).reg_value() & 1);
      })) {
    // Forcibly shutoff deblocking hardware.
    LOG(TRACE, "Forcibly DblkCtrl...");
    DblkCtrl::Get().FromValue(3).WriteTo(mmio()->dosbus);
    DblkCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      DblkStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }

  LOG(TRACE, "McStatus0 wait...");
  if (!WaitForRegister(timeout, [this] {
        return !(McStatus0::Get().ReadFrom(mmio()->dosbus).reg_value() & 1);
      })) {
    // Forcibly shutoff reference frame reading hardware.
    LOG(TRACE, "Forcibly McCtrl1...");
    auto temp = McCtrl1::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(0x9 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~0x9 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      McStatus0::Get().ReadFrom(mmio()->dosbus);
    }
  }
  LOG(TRACE, "DcacDmaCtrl wait...");
  WaitForRegister(timeout, [this] {
    return !(DcacDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000);
  });
  LOG(TRACE, "DcacDmaCtrl wait done.");
}

void Vdec1::InitializeStreamInput(bool use_parser, uint32_t buffer_address, uint32_t buffer_size) {
  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  VldMemVififoWrapCount::Get().FromValue(0).WriteTo(mmio()->dosbus);

  DosSwReset0::Get().FromValue(0).set_vdec_vld_part(1).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  Reset0Register::Get().ReadFrom(mmio()->reset);
  PowerCtlVld::Get().FromValue(1 << 4).WriteTo(mmio()->dosbus);

  VldMemVififoStartPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  VldMemVififoCurrPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  VldMemVififoEndPtr::Get().FromValue(buffer_address + buffer_size - 8).WriteTo(mmio()->dosbus);
  VldMemVififoControl::Get().FromValue(0).set_init(true).WriteTo(mmio()->dosbus);
  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).WriteTo(mmio()->dosbus);
  VldMemVififoWP::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).set_init(true).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).WriteTo(mmio()->dosbus);
  auto fifo_control =
      VldMemVififoControl::Get().FromValue(0).set_upper(0x11).set_fill_on_level(true);
  if (use_parser) {
    fifo_control.set_fill_en(true).set_empty_en(true);
  }
  // Expect input to be in normal byte order.
  fifo_control.set_endianness(7);
  fifo_control.WriteTo(mmio()->dosbus);
}

void Vdec1::InitializeParserInput() {
  VldMemVififoBufCntl::Get().FromValue(0).set_init(true).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).WriteTo(mmio()->dosbus);

  DosGenCtrl0::Get().FromValue(0).WriteTo(mmio()->dosbus);
}

void Vdec1::InitializeDirectInput() {
  VldMemVififoBufCntl::Get().FromValue(0).set_init(true).set_manual(true).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).WriteTo(mmio()->dosbus);
}

void Vdec1::UpdateWritePointer(uint32_t write_pointer) {
  VldMemVififoWP::Get().FromValue(write_pointer).WriteTo(mmio()->dosbus);
  VldMemVififoControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_fill_en(true)
      .set_empty_en(true)
      .WriteTo(mmio()->dosbus);
}

uint32_t Vdec1::GetStreamInputOffset() {
  uint32_t write_ptr = VldMemVififoWP::Get().ReadFrom(mmio()->dosbus).reg_value();
  uint32_t buffer_start = VldMemVififoStartPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  assert(write_ptr >= buffer_start);
  return write_ptr - buffer_start;
}

uint32_t Vdec1::GetReadOffset() {
  uint32_t read_ptr = VldMemVififoRP::Get().ReadFrom(mmio()->dosbus).reg_value();
  uint32_t buffer_start = VldMemVififoStartPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  assert(read_ptr >= buffer_start);
  return read_ptr - buffer_start;
}

zx_status_t Vdec1::InitializeInputContext(InputContext* context, bool is_secure) {
  constexpr uint32_t kInputContextSize = 4096;
  auto create_result = InternalBuffer::Create("VDec1InputCtx", &owner_->SysmemAllocatorSyncPtr(),
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

zx_status_t Vdec1::SaveInputContext(InputContext* context) {
  // No idea what this does.
  VldMemVififoControl::Get().FromValue(1 << 15).WriteTo(mmio()->dosbus);
  VldMemSwapAddr::Get()
      .FromValue(truncate_to_32(context->buffer->phys_base()))
      .WriteTo(mmio()->dosbus);
  VldMemSwapCtrl::Get().FromValue(0).set_enable(true).set_save(true).WriteTo(mmio()->dosbus);
  bool finished = SpinWaitForRegister(std::chrono::milliseconds(100), [this]() {
    return !VldMemSwapCtrl::Get().ReadFrom(mmio()->dosbus).in_progress();
  });
  if (!finished) {
    DECODE_ERROR("Timed out in VDec1::SaveInputContext");
    return ZX_ERR_TIMED_OUT;
  }
  VldMemSwapCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  return ZX_OK;
}

zx_status_t Vdec1::RestoreInputContext(InputContext* context) {
  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Reset the input hardware.
  DosSwReset0::Get().FromValue(0).set_vdec_vld(1).set_vdec_vld_part(1).set_vdec_vififo(1).WriteTo(
      mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Dummy read to give time for the hardware to reset.
  Reset0Register::Get().ReadFrom(mmio()->reset);
  PowerCtlVld::Get().FromValue(1 << 4).WriteTo(mmio()->dosbus);
  VldMemSwapAddr::Get()
      .FromValue(truncate_to_32(context->buffer->phys_base()))
      .WriteTo(mmio()->dosbus);
  VldMemSwapCtrl::Get().FromValue(0).set_enable(true).set_save(false).WriteTo(mmio()->dosbus);
  bool finished = SpinWaitForRegister(std::chrono::milliseconds(100), [this]() {
    return !VldMemSwapCtrl::Get().ReadFrom(mmio()->dosbus).in_progress();
  });
  if (!finished) {
    DECODE_ERROR("Timed out in VDec1::RestoreInputContext");
    return ZX_ERR_TIMED_OUT;
  }
  VldMemSwapCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  auto fifo_control =
      VldMemVififoControl::Get().FromValue(0).set_upper(0x11).set_fill_on_level(true);
  // Expect input to be in normal byte order.
  fifo_control.set_endianness(7);
  fifo_control.WriteTo(mmio()->dosbus);
  return ZX_OK;
}
