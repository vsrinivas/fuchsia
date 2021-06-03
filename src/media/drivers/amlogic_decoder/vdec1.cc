// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vdec1.h"

#include <lib/ddk/io-buffer.h>
#include <lib/trace/event.h>

#include <algorithm>

#include "device_type.h"
#include "macros.h"
#include "registers.h"
#include "src/media/lib/memory_barriers/memory_barriers.h"
#include "util.h"
#include "video_decoder.h"

namespace amlogic_decoder {

namespace {

constexpr uint32_t kFirmwareSize = 4 * 4096;

constexpr uint32_t kReadOffsetAlignment = 512;

}  // namespace

uint32_t Vdec1::vdec_sleep_bits() {
  switch (owner_->device_type()) {
    case DeviceType::kGXM:
    case DeviceType::kG12A:
    case DeviceType::kG12B:
      return 0xc;
    case DeviceType::kSM1:
      return 0x2;
  }
}

uint32_t Vdec1::vdec_iso_bits() {
  switch (owner_->device_type()) {
    case DeviceType::kGXM:
    case DeviceType::kG12A:
    case DeviceType::kG12B:
      return 0xc0;
    case DeviceType::kSM1:
      return 0x2;
  }
}

std::optional<InternalBuffer> Vdec1::LoadFirmwareToBuffer(const uint8_t* data, uint32_t len) {
  TRACE_DURATION("media", "Vdec1::LoadFirmwareToBuffer");
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

zx_status_t Vdec1::LoadFirmware(const uint8_t* data, uint32_t len) {
  auto buffer = LoadFirmwareToBuffer(data, len);
  if (!buffer)
    return ZX_ERR_NO_MEMORY;
  return LoadFirmware(*buffer);
}

zx_status_t Vdec1::LoadFirmware(InternalBuffer& buffer) {
  TRACE_DURATION("media", "Vdec1::LoadFirmware");
  ZX_DEBUG_ASSERT(buffer.size() == kFirmwareSize);
  Mpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  Cpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  ImemDmaAdr::Get().FromValue(truncate_to_32(buffer.phys_base())).WriteTo(mmio()->dosbus);
  ImemDmaCount::Get().FromValue(kFirmwareSize / sizeof(uint32_t)).WriteTo(mmio()->dosbus);
  ImemDmaCtrl::Get().FromValue(0x8000 | (7 << 16)).WriteTo(mmio()->dosbus);
  {
    TRACE_DURATION("media", "SpinWaitForRegister");

    // Measured spin wait time is around 5 microseconds on sherlock, so it makes sense to SpinWait.
    if (!SpinWaitForRegister(std::chrono::milliseconds(100), [this] {
          return (ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000) == 0;
        })) {
      DECODE_ERROR("Failed to load microcode, ImemDmaCtrl %d, ImemDmaAdr 0x%x",
                   ImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value(),
                   ImemDmaAdr::Get().ReadFrom(mmio()->dosbus).reg_value());

      BarrierBeforeRelease();
      return ZX_ERR_TIMED_OUT;
    }
  }

  BarrierBeforeRelease();
  return ZX_OK;
}

void Vdec1::PowerOn() {
  ZX_DEBUG_ASSERT(!powered_on_);
  // Make sure that the clocks are ungated before we apply power, and reset the
  // DOS unit.  In the past, we have seen a rare issue on ~3 devices where, a
  // failure to have the clocks running before the DOS unit was powered up and
  // reset, would result in a system-wide lockup. This was confirmed on only 1
  // device. The other two were production devices which could not be
  // instrumented.
  //
  // Experimental evidence seems to suggest that it may have been the main DDR
  // controller which was locking up, but it is difficult to tell right now as
  // the available documentation is extremely limited, and provides more or less
  // no guidance on the subject of a proper power on/reset sequence for this
  // unit.
  //
  // Either way, we currently let the clocks run before even powering up the DOS
  // unit.  The magic "bad" device seems to like this more than doing it after
  // resetting the DOS unit.
  owner_->UngateClocks();

  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~vdec_sleep_bits());
    temp.WriteTo(mmio()->aobus);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  DosSwReset0::Get().FromValue(0xfffffffc).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

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

  // "DECODE_CORRECTNESS_COMMENTS" (are here):
  //
  // The maximum frequency used in linux is 648 MHz, but that requires using GP0, which is already
  // being used by the GPU. The linux driver also uses 200MHz in some circumstances for videos <=
  // 1080p30.
  //
  // We'd like to pick 500 MHz, but on astro we need to run at 285.7 to avoid decode flakes at 400
  // and 500 MHz.
  //
  // However, using the h264 multi decoder, we got a few intermittent decode correctness glitches
  // when we ran at 500 MHz on astro, and still a few though less frequent at 400 MHz on astro.  At
  // 285.7 on astro we don't see those, but we do still see some on sherlock at 285.7 MHz. It's
  // possible we have something else misconfigured, or have a timing-dependent SW bug.
  //
  // For astro, running at 285.7 is very likely to be fast enough (for now) assuming linear
  // performance per clock rate.
  //
  // At 24 MHz on sherlock we don't see any decode correctness glitches, but at 285.7 and up we do.
  //
  // All flake rates below are using use-h264-multi-decoder-flake-repro-test, which uses bear.h264.
  //
  // Sherlock (one particular sherlock - the one on my desk, in the particular environment, etc):
  // 800 MHz sherlock   - ~1/12 incorrect decode (213/2529)
  // 666 MHz sherlock   - ~1/25 incorrect decode (63/1525)
  // 500 MHz sherlock   - ~1/3054 incorrect decode (6/18322)
  // 285.7 MHz sherlock - ~1/2436 incorrect decode (27/65763)
  // 24 MHz sherlock    - ~0/3156 incorrect decode (0 failures observed in 3156)
  //
  // Astro (the astro on my desk):
  // 666 MHz astro   - ~1/43 incorrect decode (50/2133)
  // 500 MHz astro   - ~1/165 incorrect decode (494/81403)
  // 400 MHz astro   - ~1/645 incorrect decode (12/7734)
  // 285.7 MHz astro - ~0/53199 incorrect decode (0/53199)
  uint32_t clock_sel;
  switch (owner_->device_type()) {
    case DeviceType::kG12A:
    case DeviceType::kG12B:
    case DeviceType::kSM1:
      clock_sel = kG12xFclkDiv7;
      break;
    case DeviceType::kGXM:
      clock_sel = kGxmFclkDiv7;
      break;
  }

  HhiVdecClkCntl::Get()
      .ReadFrom(mmio()->hiubus)
      .set_vdec_en(true)
      .set_vdec_sel(clock_sel)
      .WriteTo(mmio()->hiubus);
  owner_->ToggleClock(ClockType::kGclkVdec, true);

  DosMemPdVdec::Get().FromValue(0).WriteTo(mmio()->dosbus);

  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~vdec_iso_bits());
    temp.WriteTo(mmio()->aobus);
  }
  DosVdecMcrccStallCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
    DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_g12a_vdec(true).WriteTo(mmio()->dmc);
  } else {
    DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_vdec(true).WriteTo(mmio()->dmc);
  }

  MdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus).set_bit31(false).WriteTo(mmio()->dosbus);

  // Reset all the hardware again. Doing it at this time doesn't match the linux driver, but instead
  // matches the hardware documentation. If we don't do this, restoring the input context or loading
  // the firmware can hang.
  DosSwReset0::Get().FromValue(0xfffffffc).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);
  powered_on_ = true;
}

void Vdec1::PowerOff() {
  ZX_DEBUG_ASSERT(powered_on_);
  powered_on_ = false;
  if (IsDeviceAtLeast(owner_->device_type(), DeviceType::kG12A)) {
    DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_g12a_vdec(false).WriteTo(mmio()->dmc);
  } else {
    DmcReqCtrl::Get().ReadFrom(mmio()->dmc).set_vdec(false).WriteTo(mmio()->dmc);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | vdec_iso_bits());
    temp.WriteTo(mmio()->aobus);
  }
  DosMemPdVdec::Get().FromValue(~0u).WriteTo(mmio()->dosbus);
  HhiVdecClkCntl::Get().ReadFrom(mmio()->hiubus).set_vdec_en(false).WriteTo(mmio()->hiubus);

  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | vdec_sleep_bits());
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

  DosSwReset0::Get().FromValue(0).set_vdec_ccpu(1).set_vdec_mcpu(1).WriteTo(mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Delay to ensure previous write have executed.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(mmio()->dosbus);
  }
}

void Vdec1::WaitForIdle() {
  auto timeout = std::chrono::milliseconds(100);
  LOG(DEBUG, "MdecPicDcStatus wait...");
  if (!WaitForRegister(timeout, [this] {
        return MdecPicDcStatus::Get().ReadFrom(mmio()->dosbus).reg_value() == 0;
      })) {
    // Forcibly shutoff video output hardware. Probably.
    LOG(DEBUG, "Forcibly MdecPicDcCtrl...");
    auto temp = MdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(1 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~1 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      MdecPicDcStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }
  LOG(DEBUG, "DblkStatus wait...");
  if (!WaitForRegister(timeout, [this] {
        return !(DblkStatus::Get().ReadFrom(mmio()->dosbus).reg_value() & 1);
      })) {
    // Forcibly shutoff deblocking hardware.
    LOG(DEBUG, "Forcibly DblkCtrl...");
    DblkCtrl::Get().FromValue(3).WriteTo(mmio()->dosbus);
    DblkCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      DblkStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }

  LOG(DEBUG, "McStatus0 wait...");
  if (!WaitForRegister(timeout, [this] {
        return !(McStatus0::Get().ReadFrom(mmio()->dosbus).reg_value() & 1);
      })) {
    // Forcibly shutoff reference frame reading hardware.
    LOG(DEBUG, "Forcibly McCtrl1...");
    auto temp = McCtrl1::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(0x9 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~0x9 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      McStatus0::Get().ReadFrom(mmio()->dosbus);
    }
  }
  LOG(DEBUG, "DcacDmaCtrl wait...");
  WaitForRegister(timeout, [this] {
    return !(DcacDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() & 0x8000);
  });
  LOG(DEBUG, "DcacDmaCtrl wait done.");
}

void Vdec1::InitializeStreamInput(bool use_parser, uint32_t buffer_address, uint32_t buffer_size) {
  ZX_DEBUG_ASSERT(buffer_size % kReadOffsetAlignment == 0);

  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);
  VldMemVififoWrapCount::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // These reset bits avoid the fifo leaking in data.  With these bits we can cleanly re-start
  // decode without stale fifo bits leaking in.  This allows using InitializeStreamInput() to
  // re-start decode almost as if we're restoring a saved input context.
  DosSwReset0::Get().FromValue(0).set_vdec_vld(1).set_vdec_vld_part(1).set_vdec_vififo(1).WriteTo(
      mmio()->dosbus);
  DosSwReset0::Get().FromValue(0).WriteTo(mmio()->dosbus);

  Reset0Register::Get().ReadFrom(mmio()->reset);

  auto temp = PowerCtlVld::Get().ReadFrom(mmio()->dosbus);
  temp.set_reg_value(temp.reg_value() | (1 << 4) | (1 << 6) | (1 << 9));
  temp.WriteTo(mmio()->dosbus);

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

void Vdec1::UpdateWriteOffset(uint32_t write_offset) {
  uint32_t buffer_start = VldMemVififoStartPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  assert(buffer_start + write_offset >= buffer_start);
  UpdateWritePointer(buffer_start + write_offset);
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
  context->buffer->CacheFlush(0, context->buffer->size());
  BarrierAfterFlush();

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

  auto temp = PowerCtlVld::Get().ReadFrom(mmio()->dosbus);
  temp.set_reg_value(temp.reg_value() | (1 << 4));
  // Power on various parts of the VLD hardware. This needs to be done before
  // swapping in or else some state will remain uninitialized. Bit 9 holds
  // information related to the escape sequence status.
  const bool kH264Video = true;
  if (kH264Video)
    temp.set_reg_value(temp.reg_value() | (1 << 6) | (1 << 9));
  temp.WriteTo(mmio()->dosbus);

  VldMemVififoControl::Get().FromValue(0).WriteTo(mmio()->dosbus);

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

}  // namespace amlogic_decoder
