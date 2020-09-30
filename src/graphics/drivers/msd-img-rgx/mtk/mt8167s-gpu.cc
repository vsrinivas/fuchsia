// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167s-gpu.h"

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/clock.h>

#include <iterator>
#include <memory>
#include <mutex>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/algorithm.h>
#include <hw/reg.h>

#include "img-sys-device.h"
#include "magma_util/macros.h"
#include "platform_trace.h"
#include "platform_trace_provider.h"
#include "platform_trace_provider_with_fdio.h"
#include "sys_driver/magma_driver.h"

#define GPU_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#if MAGMA_TEST_DRIVER
void magma_indriver_test(zx_device_t* device, void* driver_device_handle);
#endif

using FidlStatus = llcpp::fuchsia::gpu::magma::Status;

namespace {
struct FragmentDescription {
  static constexpr uint32_t kPowerResetBBit = 0;
  static constexpr uint32_t kPowerIsoBit = 1;
  static constexpr uint32_t kPowerOnBit = 2;
  static constexpr uint32_t kPowerOn2ndBit = 3;
  static constexpr uint32_t kPowerOnClkDisBit = 4;

  zx_status_t PowerOn(ddk::MmioBuffer* power_gpu_buffer);
  zx_status_t PowerDown(ddk::MmioBuffer* power_gpu_buffer);
  bool IsPoweredOn(ddk::MmioBuffer* power_gpu_buffer, uint32_t bit);

  // offset into power_gpu_buffer registers
  uint32_t reg_offset;
  // Index into the power status registers, used to determine when powered on.
  uint32_t on_bit_offset;
  // Bits in the register that need to be set to zero to power on the SRAM.
  uint32_t sram_bits;
  // Bits in the register that will be cleared once the SRAM is powered on.
  uint32_t sram_ack_bits;
};

zx_status_t FragmentDescription::PowerOn(ddk::MmioBuffer* power_gpu_buffer) {
  power_gpu_buffer->SetBit<uint32_t>(kPowerOnBit, reg_offset);
  power_gpu_buffer->SetBit<uint32_t>(kPowerOn2ndBit, reg_offset);
  zx::time timeout = zx::deadline_after(zx::msec(100));  // Arbitrary timeout
  while (!IsPoweredOn(power_gpu_buffer, on_bit_offset)) {
    if (zx::clock::get_monotonic() > timeout) {
      GPU_ERROR("Timed out powering on fragment");
      return ZX_ERR_TIMED_OUT;
    }
  }
  power_gpu_buffer->ClearBit<uint32_t>(kPowerOnClkDisBit, reg_offset);
  power_gpu_buffer->ClearBit<uint32_t>(kPowerIsoBit, reg_offset);
  power_gpu_buffer->SetBit<uint32_t>(kPowerResetBBit, reg_offset);
  if (sram_bits) {
    power_gpu_buffer->ClearBits32(sram_bits, reg_offset);
    zx::time timeout = zx::deadline_after(zx::msec(100));  // Arbitrary timeout
    while (power_gpu_buffer->ReadMasked32(sram_ack_bits, reg_offset)) {
      if (zx::clock::get_monotonic() > timeout) {
        GPU_ERROR("Timed out powering on SRAM");
        return ZX_ERR_TIMED_OUT;
      }
    }
  }
  return ZX_OK;
}

zx_status_t FragmentDescription::PowerDown(ddk::MmioBuffer* power_gpu_buffer) {
  if (sram_bits) {
    power_gpu_buffer->SetBits32(sram_bits, reg_offset);
    zx::time timeout = zx::deadline_after(zx::msec(100));  // Arbitrary timeout
    while (power_gpu_buffer->ReadMasked32(sram_ack_bits, reg_offset) != sram_ack_bits) {
      if (zx::clock::get_monotonic() > timeout) {
        GPU_ERROR("Timed out powering down SRAM");
        return ZX_ERR_TIMED_OUT;
      }
    }
  }

  power_gpu_buffer->SetBit<uint32_t>(kPowerIsoBit, reg_offset);
  power_gpu_buffer->ClearBit<uint32_t>(kPowerResetBBit, reg_offset);
  power_gpu_buffer->SetBit<uint32_t>(kPowerOnClkDisBit, reg_offset);
  power_gpu_buffer->ClearBit<uint32_t>(kPowerOnBit, reg_offset);
  power_gpu_buffer->ClearBit<uint32_t>(kPowerOn2ndBit, reg_offset);

  zx::time timeout = zx::deadline_after(zx::msec(100));  // Arbitrary timeout
  while (IsPoweredOn(power_gpu_buffer, on_bit_offset)) {
    if (zx::clock::get_monotonic() > timeout) {
      GPU_ERROR("Timed out powering down fragment");
      return ZX_ERR_TIMED_OUT;
    }
  }
  return ZX_OK;
}

bool FragmentDescription::IsPoweredOn(ddk::MmioBuffer* power_gpu_buffer, uint32_t bit) {
  return power_gpu_buffer->GetBit<uint32_t>(bit, kPwrStatus) &&
         power_gpu_buffer->GetBit<uint32_t>(bit, kPwrStatus2nd);
}

static FragmentDescription MfgAsyncFragment() {
  constexpr uint32_t kAsyncPwrStatusBit = 25;
  constexpr uint32_t kAsyncPwrRegOffset = 0x2c4;
  return FragmentDescription{kAsyncPwrRegOffset, kAsyncPwrStatusBit, 0, 0};
}

static FragmentDescription Mfg2dFragment() {
  constexpr uint32_t k2dPwrStatusBit = 24;
  constexpr uint32_t k2dPwrRegOffset = 0x2c0;
  constexpr uint32_t kSramPdMask = 0xf << 8;
  constexpr uint32_t kSramPdAckMask = 0xf << 12;
  return FragmentDescription{k2dPwrRegOffset, k2dPwrStatusBit, kSramPdMask, kSramPdAckMask};
}

}  // namespace

Mt8167sGpu::~Mt8167sGpu() {
  std::lock_guard<std::mutex> lock(magma_mutex_);
  StopMagma();
}

bool Mt8167sGpu::StartMagma() {
  magma_system_device_ = magma_driver_->CreateDevice(static_cast<ImgSysDevice*>(this));
  return !!magma_system_device_;
}

void Mt8167sGpu::StopMagma() {
  if (magma_system_device_) {
    magma_system_device_->Shutdown();
    magma_system_device_.reset();
  }
}

void Mt8167sGpu::DdkRelease() { delete this; }

zx_status_t Mt8167sGpu::DdkMessage(fidl_msg_t* message, fidl_txn_t* transaction) {
  DdkTransaction ddk_transaction(transaction);
  llcpp::fuchsia::gpu::magma::Device::Dispatch(this, message, &ddk_transaction);
  return ddk_transaction.Status();
}
// Power on the asynchronous memory interface between the GPU and the DDR controller.
zx_status_t Mt8167sGpu::PowerOnMfgAsync() {
  // Set clock sources properly. Some of these are also used by the 3D and 2D
  // cores.
  clock_gpu_buffer_->ModifyBits<uint32_t>(0, 20, 2, 0x40);  // slow mfg mux to 26MHz
  // MFG AXI to mainpll_d11 (on version 2+ of chip)
  clock_gpu_buffer_->ModifyBits<uint32_t>(1, 18, 2, 0x40);
  clks_[kClkSlowMfgIndex].Enable();
  clks_[kClkAxiMfgIndex].Enable();
  return MfgAsyncFragment().PowerOn(&power_gpu_buffer_.value());
}

// Power on the 2D engine (it's unclear whether this is needed to access the 3D
// GPU, but power it on anyway).
zx_status_t Mt8167sGpu::PowerOnMfg2d() {
  // Enable access to AXI Bus
  clock_gpu_buffer_->SetBits32(kInfraTopAxiSi1WayEnMfg2d, kInfraTopAxiSi1Ctl);

  zx_status_t status = Mfg2dFragment().PowerOn(&power_gpu_buffer_.value());
  if (status != ZX_OK)
    return status;
  // Disable AXI protection after it's powered up.
  clock_gpu_buffer_->ClearBits32(kInfraTopAxiBusProtMaskMfg2d, kInfraTopAxiProtectEn);
  zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
  return ZX_OK;
}

// Power on the 3D engine (IMG GPU).
zx_status_t Mt8167sGpu::PowerOnMfg() {
  clks_[kClkMfgMmIndex].Enable();
  // The APM should handle actually powering up the MFG fragment as needed,
  // so that doesn't need to be done here.

  // Enable clocks in MFG (using controls internal to MFG_TOP)
  constexpr uint32_t kMfgCgClr = 0x8;
  constexpr uint32_t kBAxiClr = (1 << 0);
  constexpr uint32_t kBMemClr = (1 << 1);
  constexpr uint32_t kBG3dClr = (1 << 2);
  constexpr uint32_t kB26MClr = (1 << 3);
  gpu_buffer_->SetBits32(kBAxiClr | kBMemClr | kBG3dClr | kB26MClr, kMfgCgClr);
  EnableMfgHwApm();
  return ZX_OK;
}

// Power down the asynchronous memory interface between the GPU and the DDR controller.
zx_status_t Mt8167sGpu::PowerDownMfgAsync() {
  zx_status_t status = MfgAsyncFragment().PowerDown(&power_gpu_buffer_.value());
  if (status != ZX_OK) {
    return status;
  }
  clks_[kClkAxiMfgIndex].Disable();
  clks_[kClkSlowMfgIndex].Disable();
  return ZX_OK;
}

// Power down the 2D engine.
zx_status_t Mt8167sGpu::PowerDownMfg2d() {
  // Enable AXI protection
  clock_gpu_buffer_->SetBits32(kInfraTopAxiBusProtMaskMfg2d, kInfraTopAxiProtectEn);

  zx_status_t status = Mfg2dFragment().PowerDown(&power_gpu_buffer_.value());
  if (status != ZX_OK)
    return status;
  // Disable access to AXI Bus
  clock_gpu_buffer_->ClearBits32(kInfraTopAxiSi1WayEnMfg2d, kInfraTopAxiSi1Ctl);
  return ZX_OK;
}

// Power down the 3D engine (IMG GPU).
zx_status_t Mt8167sGpu::PowerDownMfg() {
  // Disable clocks in MFG (using controls internal to MFG_TOP)
  constexpr uint32_t kMfgCgSet = 0x4;
  constexpr uint32_t kBAxiClr = (1 << 0);
  constexpr uint32_t kBMemClr = (1 << 1);
  constexpr uint32_t kBG3dClr = (1 << 2);
  constexpr uint32_t kB26MClr = (1 << 3);
  gpu_buffer_->SetBits32(kBAxiClr | kBMemClr | kBG3dClr | kB26MClr, kMfgCgSet);

  // The APM should handle actually powering down the MFG fragment as needed,
  // so that doesn't need to be done here.

  // Disable MFG clock.
  clks_[kClkMfgMmIndex].Disable();
  return ZX_OK;
}

// Enable hardware-controlled power management.
void Mt8167sGpu::EnableMfgHwApm() {
  struct {
    uint32_t value;
    uint32_t offset;
  } writes[] = {
      {0x01a80000, 0x504}, {0x00080010, 0x508}, {0x00080010, 0x50c}, {0x00b800b8, 0x510},
      {0x00b000b0, 0x514}, {0x00c000c8, 0x518}, {0x00c000c8, 0x51c}, {0x00d000d8, 0x520},
      {0x00d800d8, 0x524}, {0x00d800d8, 0x528}, {0x9000001b, 0x24},  {0x8000001b, 0x24},
  };

  for (uint32_t i = 0; i < countof(writes); i++) {
    gpu_buffer_->Write32(writes[i].value, writes[i].offset);
  }
}

static uint64_t ReadHW64(const ddk::MmioBuffer* buffer, uint32_t offset) {
  // Read 2 registers to combine into a 64-bit register.
  return (static_cast<uint64_t>(buffer->Read32(offset + 4)) << 32) | buffer->Read32(offset);
}

zx_status_t Mt8167sGpu::PowerUp() {
  // Power on in order.
  zx_status_t status = PowerOnMfgAsync();
  if (status != ZX_OK) {
    GPU_ERROR("Failed to power on MFG ASYNC\n");
    return status;
  }
  status = PowerOnMfg2d();
  if (status != ZX_OK) {
    GPU_ERROR("Failed to power on MFG 2D\n");
    return status;
  }
  status = PowerOnMfg();
  if (status != ZX_OK) {
    GPU_ERROR("Failed to power on MFG\n");
    return status;
  }
  if (!logged_gpu_info_) {
    constexpr uint32_t kRgxCrCoreId = 0x18;
    constexpr uint32_t kRgxCrCoreRevision = 0x20;

    zxlogf(INFO, "[mt8167s-gpu] GPU ID: %lx", ReadHW64(&real_gpu_buffer_.value(), kRgxCrCoreId));
    zxlogf(INFO, "[mt8167s-gpu] GPU core revision: %lx",
           ReadHW64(&real_gpu_buffer_.value(), kRgxCrCoreRevision));
    logged_gpu_info_ = true;
  }

  return ZX_OK;
}

zx_status_t Mt8167sGpu::PowerDown() {
  DLOG("Mt8167sGpu::PowerDown() start");
  // Power down in the opposite order they were powered up.
  zx_status_t status = PowerDownMfg();
  if (status != ZX_OK) {
    GPU_ERROR("Failed to power down MFG\n");
    return status;
  }
  status = PowerDownMfg2d();
  if (status != ZX_OK) {
    GPU_ERROR("Failed to power down MFG 2D\n");
    return status;
  }

  status = PowerDownMfgAsync();
  if (status != ZX_OK) {
    GPU_ERROR("Failed to power down MFG ASYNC\n");
    return status;
  }
  DLOG("Mt8167sGpu::PowerDown() done");
  return ZX_OK;
}

zx_status_t Mt8167sGpu::Bind() {
  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    GPU_ERROR("ZX_PROTOCOL_COMPOSITE not available\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Zeroth fragment is pdev
  zx_device_t* fragments[kClockCount + 1];
  size_t actual;
  composite.GetFragments(fragments, std::size(fragments), &actual);
  if (actual != std::size(fragments)) {
    GPU_ERROR("could not retrieve all our fragments\n");
    return ZX_ERR_INTERNAL;
  }

  for (unsigned i = 0; i < kClockCount; i++) {
    clks_[i] = fragments[i + 1];
    if (!clks_[i].is_valid()) {
      zxlogf(ERROR, "%s could not get clock", __func__);
      return ZX_ERR_INTERNAL;
    }
  }

  ddk::PDev pdev(fragments[0]);
  auto status = pdev.MapMmio(kMfgMmioIndex, &real_gpu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }
  status = pdev.MapMmio(kMfgTopMmioIndex, &gpu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }
  status = pdev.MapMmio(kScpsysMmioIndex, &power_gpu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }
  status = pdev.MapMmio(kXoMmioIndex, &clock_gpu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }

#if MAGMA_TEST_DRIVER
  DLOG("running magma indriver test");
  magma_indriver_test(parent(), this);
#endif

  {
    std::lock_guard<std::mutex> lock(magma_mutex_);
    magma_driver_ = MagmaDriver::Create();
    if (!magma_driver_) {
      GPU_ERROR("Failed to create MagmaDriver\n");
      return ZX_ERR_INTERNAL;
    }

    if (!StartMagma()) {
      GPU_ERROR("Failed to start Magma system device\n");
      return ZX_ERR_INTERNAL;
    }
  }

  return DdkAdd("mt8167s-gpu");
}

void Mt8167sGpu::Query2(uint64_t query_id, Query2Completer::Sync _completer) {
  DLOG("Mt8167sGpu::Query");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  uint64_t result;
  switch (query_id) {
    case MAGMA_QUERY_DEVICE_ID:
      result = magma_system_device_->GetDeviceId();
      break;
    case MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED:
#if MAGMA_TEST_DRIVER
      result = 1;
#else
      result = 0;
#endif
      break;
    default:
      magma::Status status = magma_system_device_->Query(query_id, &result);
      if (!status.ok()) {
        _completer.ReplyError(static_cast<FidlStatus>(status.getFidlStatus()));
        return;
      }
  }
  DLOG("query query_id 0x%" PRIx64 " returning 0x%" PRIx64, query_id, result);

  _completer.ReplySuccess(result);
}

void Mt8167sGpu::QueryReturnsBuffer(uint64_t query_id,
                                    QueryReturnsBufferCompleter::Sync _completer) {
  DLOG("Mt8167sGpu::QueryReturnsBuffer");
  std::lock_guard<std::mutex> lock(magma_mutex_);
  zx_handle_t result;
  magma::Status status = magma_system_device_->QueryReturnsBuffer(query_id, &result);
  if (!status.ok()) {
    _completer.ReplyError(static_cast<FidlStatus>(status.getFidlStatus()));
    return;
  }
  DLOG("query exteneded query_id 0x%" PRIx64 " returning 0x%x", query_id, result);

  _completer.ReplySuccess(zx::vmo(result));
}

void Mt8167sGpu::Connect(uint64_t client_id, ConnectCompleter::Sync _completer) {
  DLOG("Mt8167sGpu::Connect");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  auto connection =
      MagmaSystemDevice::Open(magma_system_device_, client_id, /*thread_profile*/ nullptr);
  if (!connection) {
    _completer.Close(ZX_ERR_INTERNAL);
    return;
  }

  _completer.Reply(zx::channel(connection->GetClientEndpoint()),
                   zx::channel(connection->GetClientNotificationEndpoint()));

  magma_system_device_->StartConnectionThread(std::move(connection));
}

void Mt8167sGpu::DumpState(uint32_t dump_type, DumpStateCompleter::Sync _completer) {
  DLOG("Mt8167sGpu::DumpState");
  std::lock_guard<std::mutex> lock(magma_mutex_);
  if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                    MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE)) {
    DLOG("Invalid dump type %x", dump_type);
    return;
  }
  if (magma_system_device_)
    magma_system_device_->DumpStatus(dump_type);
}

void Mt8167sGpu::TestRestart(TestRestartCompleter::Sync _completer) {
  DLOG("Mt8167sGpu::TestRestart");
#if MAGMA_TEST_DRIVER
  std::lock_guard<std::mutex> lock(magma_mutex_);
  StopMagma();
  if (!StartMagma()) {
    DLOG("StartMagma failed");
  }
#endif
}

void Mt8167sGpu::GetUnitTestStatus(GetUnitTestStatusCompleter::Sync _completer) {
  _completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

extern "C" zx_status_t mt8167s_gpu_bind(void* ctx, zx_device_t* parent) {
  if (magma::PlatformTraceProvider::Get())
    magma::InitializeTraceProviderWithFdio(magma::PlatformTraceProvider::Get());
  auto dev = std::make_unique<Mt8167sGpu>(parent);
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t mt8167s_gpu_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = mt8167s_gpu_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(mt8167s_gpu, mt8167s_gpu_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_GPU),
ZIRCON_DRIVER_END(mt8167s_gpu)
