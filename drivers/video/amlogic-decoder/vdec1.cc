// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vdec1.h"

#include <ddk/io-buffer.h>

#include <algorithm>

#include "macros.h"

zx_status_t Vdec1::LoadFirmware(const uint8_t* data, uint32_t size) {
  Mpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  Cpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  io_buffer_t firmware_buffer;
  const uint32_t kFirmwareSize = 4 * 4096;
  // Most buffers should be 64-kbyte aligned.
  const uint32_t kBufferAlignShift = 16;
  zx_status_t status = io_buffer_init_aligned(&firmware_buffer, owner_->bti(),
                                              kFirmwareSize, kBufferAlignShift,
                                              IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to make firmware buffer");
    return status;
  }

  memcpy(io_buffer_virt(&firmware_buffer), data, std::min(size, kFirmwareSize));
  io_buffer_cache_flush(&firmware_buffer, 0, kFirmwareSize);

  ImemDmaAdr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&firmware_buffer)))
      .WriteTo(mmio()->dosbus);
  ImemDmaCount::Get()
      .FromValue(kFirmwareSize / sizeof(uint32_t))
      .WriteTo(mmio()->dosbus);
  ImemDmaCtrl::Get().FromValue(0x8000 | (7 << 16)).WriteTo(mmio()->dosbus);

  if (!WaitForRegister(std::chrono::milliseconds(100), [this] {
        return (ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() &
                0x8000) == 0;
      })) {
    DECODE_ERROR("Failed to load microcode.");

    io_buffer_release(&firmware_buffer);
    return ZX_ERR_TIMED_OUT;
  }

  io_buffer_release(&firmware_buffer);
  return ZX_OK;
}

void Vdec1::PowerOn() {
  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc);
    temp.WriteTo(mmio()->aobus);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  DosSwReset0::Get().FromValue(0xfffffffc).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  owner_->UngateClocks();

  HhiVdecClkCntl::Get().FromValue(0).set_vdec_en(true).set_vdec_sel(3).WriteTo(
      mmio()->hiubus);
  DosGclkEn::Get().FromValue(0x3ff).WriteTo(mmio()->dosbus);
  DosMemPdVdec::Get().FromValue(0).WriteTo(mmio()->dosbus);
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  DosVdecMcrccStallCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_vdec(true).WriteTo(mmio()->dmc);

  MdecPicDcCtrl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_bit31(false)
      .WriteTo(mmio()->dosbus);
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
  HhiVdecClkCntl::Get().FromValue(0).set_vdec_en(false).set_vdec_sel(3).WriteTo(
      mmio()->hiubus);

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

  DosSwReset0::Get().FromValue((1 << 12) | (1 << 11)).WriteTo(mmio()->dosbus);
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
        return (ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() &
                0x8000) == 0;
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
  if (!WaitForRegister(timeout, [this] {
        return MdecPicDcStatus::Get().ReadFrom(mmio()->dosbus).reg_value() == 0;
      })) {
    // Forcibly shutoff video output hardware. Probably.
    auto temp = MdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(1 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~1 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      MdecPicDcStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }
  if (!WaitForRegister(timeout, [this] {
        return DblkStatus::Get().ReadFrom(mmio()->dosbus).reg_value() == 0;
      })) {
    // Forcibly shutoff deblocking hardware.
    DblkCtrl::Get().FromValue(3).WriteTo(mmio()->dosbus);
    DblkCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      DblkStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }

  if (!WaitForRegister(timeout, [this] {
        return McStatus0::Get().ReadFrom(mmio()->dosbus).reg_value() == 0;
      })) {
    // Forcibly shutoff reference frame reading hardware.
    auto temp = McCtrl1::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(0x9 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~0x9 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      McStatus0::Get().ReadFrom(mmio()->dosbus);
    }
  }
  WaitForRegister(timeout, [this] {
    return !(DcacDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000);
  });
}

void Vdec1::InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                                  uint32_t buffer_size) {
  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  VldMemVififoWrapCount::Get().FromValue(0).WriteTo(mmio()->dosbus);

  DosSwReset0::Get().FromValue(1 << 4).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  Reset0Register::Get().ReadFrom(mmio()->reset);
  PowerCtlVld::Get().FromValue(1 << 4).WriteTo(mmio()->dosbus);

  VldMemVififoStartPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  VldMemVififoCurrPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  VldMemVififoEndPtr::Get()
      .FromValue(buffer_address + buffer_size - 8)
      .WriteTo(mmio()->dosbus);
  VldMemVififoControl::Get().FromValue(0).set_init(true).WriteTo(
      mmio()->dosbus);
  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).WriteTo(
      mmio()->dosbus);
  VldMemVififoWP::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get()
      .FromValue(0)
      .set_manual(true)
      .set_init(true)
      .WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).WriteTo(
      mmio()->dosbus);
  auto fifo_control =
      VldMemVififoControl::Get().FromValue(0).set_upper(0x11).set_fill_on_level(
          true);
  if (use_parser) {
    fifo_control.set_fill_en(true).set_empty_en(true);
    // Parser will do 64-bit endianness conversion.
    fifo_control.set_endianness(0);
  } else {
    // Expect input to be in normal byte order.
    fifo_control.set_endianness(7);
  }
  fifo_control.WriteTo(mmio()->dosbus);
}

void Vdec1::InitializeParserInput() {
  VldMemVififoBufCntl::Get().FromValue(0).set_init(true).WriteTo(
      mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).WriteTo(mmio()->dosbus);

  DosGenCtrl0::Get().FromValue(0).WriteTo(mmio()->dosbus);
}

void Vdec1::InitializeDirectInput() {
  VldMemVififoBufCntl::Get()
      .FromValue(0)
      .set_init(true)
      .set_manual(true)
      .WriteTo(mmio()->dosbus);
  VldMemVififoBufCntl::Get().FromValue(0).set_manual(true).WriteTo(
      mmio()->dosbus);
}
