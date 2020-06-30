// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-nna.h"

#include <stdlib.h>
#include <unistd.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "t931-nna-regs.h"

namespace {

// PWR Shifts
constexpr uint32_t kPowerSleepSetPowerOff = 16;
constexpr uint32_t kPowerIsoSetIsolated = 16;

// CLK Shifts
constexpr uint32_t kClockCoreEnableShift = 8;
constexpr uint32_t kClockAxiEnableShift = 24;

// constexpr uint32_t kNna = 0;
constexpr uint32_t kHiu = 1;
constexpr uint32_t kPowerDomain = 2;
constexpr uint32_t kMemoryDomain = 3;
}  // namespace

namespace aml_nna {

// This is to be compatible with magma::ZirconPlatformDevice.
zx_status_t AmlNnaDevice::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = parent_pdev_.ctx;
  switch (proto_id) {
    case ZX_PROTOCOL_PDEV:
      proto->ops = parent_pdev_.ops;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void AmlNnaDevice::Init() {
  // Additional unnamed bits are set to match the reference amlogic source.
  // set bit[16-17]=0
  power_mmio_.ClearBits32(1 << kPowerSleepSetPowerOff | 1 << 17, AO_RTI_GEN_PWR_SLEEP0);

  // set bit[16-17]=0
  power_mmio_.ClearBits32(1 << kPowerIsoSetIsolated | 1 << 17, AO_RTI_GEN_PWR_ISO0);

  // MEM_PD_REG0 set 0
  memory_pd_mmio_.Write32(0, HHI_NANOQ_MEM_PD_REG0);

  // MEM_PD_REG1 set 0
  memory_pd_mmio_.Write32(0, HHI_NANOQ_MEM_PD_REG1);

  // Setup Clocks.
  // Set clocks to 800 MHz (FCLK_DIV2P5 = 3, Divisor = 1)
  // VIPNANOQ Core clock
  hiu_mmio_.SetBits32(((1 << kClockCoreEnableShift) | 3 << 9), HHI_VIPNANOQ_CLK_CNTL);
  // VIPNANOQ Axi clock
  hiu_mmio_.SetBits32(((1 << kClockAxiEnableShift) | 3 << 25), HHI_VIPNANOQ_CLK_CNTL);
}

// static
zx_status_t AmlNnaDevice::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  pdev_protocol_t proto;

  status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &proto);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get PDEV protocol, err: %d\n", __func__, status);
    return status;
  }

  ddk::PDev pdev(parent);

  std::optional<ddk::MmioBuffer> hiu_mmio;
  status = pdev.MapMmio(kHiu, &hiu_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> power_mmio;
  status = pdev.MapMmio(kPowerDomain, &power_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> memory_pd_mmio;
  status = pdev.MapMmio(kMemoryDomain, &memory_pd_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;

  auto device = std::unique_ptr<AmlNnaDevice>(new (&ac) AmlNnaDevice(
      parent, std::move(*hiu_mmio), std::move(*power_mmio), std::move(*memory_pd_mmio), proto));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  device->Init();

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PDEV},
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      // TODO(fxb/53525): may want to rename this. This is to match the current msd-vsl-gc driver.
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GPU_VSL_GC},
  };

  status = device->DdkAdd(ddk::DeviceAddArgs("aml-nna").set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_nna: Could not create aml nna device: %d\n", status);
    return status;
  }
  zxlogf(INFO, "aml_nna: Added aml_nna device\n");

  // intentionally leaked as it is now held by DevMgr.
  __UNUSED auto ptr = device.release();
  return status;
}

void AmlNnaDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlNnaDevice::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlNnaDevice::Create;
  return ops;
}();

}  // namespace aml_nna

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_nna, aml_nna::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_NNA),
ZIRCON_DRIVER_END(aml_nna)
