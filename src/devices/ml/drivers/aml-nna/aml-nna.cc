// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-nna.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/types.h>

#include <memory>

#include <bind/fuchsia/platform/cpp/fidl.h>
#include <bind/fuchsia/verisilicon/platform/cpp/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "s905d3-nna-regs.h"
#include "src/devices/ml/drivers/aml-nna/aml_nna_bind.h"
#include "t931-nna-regs.h"

namespace {

// CLK Shifts
constexpr uint32_t kClockCoreEnableShift = 8;
constexpr uint32_t kClockAxiEnableShift = 24;

// constexpr uint32_t kNna = 0;
constexpr uint32_t kHiu = 1;
constexpr uint32_t kPowerDomain = 2;
constexpr uint32_t kMemoryDomain = 3;
// constexpr uint32_t kSram = 5;
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

zx_status_t AmlNnaDevice::Init() {
  power_mmio_.ClearBits32(nna_block_.domain_power_sleep_bits, nna_block_.domain_power_sleep_offset);

  memory_pd_mmio_.Write32(0, nna_block_.hhi_mem_pd_reg0_offset);

  memory_pd_mmio_.Write32(0, nna_block_.hhi_mem_pd_reg1_offset);

  // set bit[12]=0
  auto clear_result = reset_.WriteRegister32(nna_block_.reset_level2_offset,
                                             aml_registers::NNA_RESET2_LEVEL_MASK, 0);
  if ((clear_result.status() != ZX_OK) || clear_result->result.is_err()) {
    zxlogf(ERROR, "%s: Clear Reset Write failed\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  power_mmio_.ClearBits32(nna_block_.domain_power_iso_bits, nna_block_.domain_power_iso_offset);

  // set bit[12]=1
  auto set_result =
      reset_.WriteRegister32(nna_block_.reset_level2_offset, aml_registers::NNA_RESET2_LEVEL_MASK,
                             aml_registers::NNA_RESET2_LEVEL_MASK);
  if ((set_result.status() != ZX_OK) || set_result->result.is_err()) {
    zxlogf(ERROR, "%s: Set Reset Write failed\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  // Setup Clocks.
  // Set clocks to 800 MHz (FCLK_DIV2P5 = 3, Divisor = 1)
  // VIPNANOQ Core clock
  hiu_mmio_.SetBits32(((1 << kClockCoreEnableShift) | 3 << 9), nna_block_.clock_control_offset);
  // VIPNANOQ Axi clock
  hiu_mmio_.SetBits32(((1 << kClockAxiEnableShift) | 3 << 25), nna_block_.clock_control_offset);

  return ZX_OK;
}

// static
zx_status_t AmlNnaDevice::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::PDev pdev = ddk::PDev::FromFragment(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "Could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }
  ddk::RegistersProtocolClient reset(parent, "register-reset");
  if (!reset.is_valid()) {
    zxlogf(ERROR, "Could not get reset_register fragment");
    return ZX_ERR_NO_RESOURCES;
  }
  zx::channel client_end, server_end;
  if ((status = zx::channel::create(0, &client_end, &server_end)) != ZX_OK) {
    zxlogf(ERROR, "Could not create channel %d\n", status);
    return status;
  }
  reset.Connect(std::move(server_end));

  std::optional<ddk::MmioBuffer> hiu_mmio;
  status = pdev.MapMmio(kHiu, &hiu_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pdev_.MapMmio failed %d\n", status);
    return status;
  }

  std::optional<ddk::MmioBuffer> power_mmio;
  status = pdev.MapMmio(kPowerDomain, &power_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pdev_.MapMmio failed %d\n", status);
    return status;
  }

  std::optional<ddk::MmioBuffer> memory_pd_mmio;
  status = pdev.MapMmio(kMemoryDomain, &memory_pd_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pdev_.MapMmio failed %d\n", status);
    return status;
  }

  pdev_device_info_t info;
  status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pdev_.GetDeviceInfo failed %d\n", status);
    return status;
  }

  NnaBlock nna_block;
  switch (info.pid) {
    case PDEV_PID_AMLOGIC_A311D:
    case PDEV_PID_AMLOGIC_T931:
      nna_block = T931NnaBlock;
      break;
    case PDEV_PID_AMLOGIC_S905D3:
      nna_block = S905d3NnaBlock;
      break;
    default:
      zxlogf(ERROR, "unhandled PID 0x%x", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;

  auto device = std::unique_ptr<AmlNnaDevice>(new (&ac) AmlNnaDevice(
      parent, std::move(*hiu_mmio), std::move(*power_mmio), std::move(*memory_pd_mmio),
      std::move(client_end), std::move(pdev), nna_block));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    zxlogf(ERROR, "Could not init device %d.", status);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, bind::fuchsia::platform::BIND_PROTOCOL_DEVICE},
      {BIND_PLATFORM_DEV_VID, 0,
       bind::fuchsia::verisilicon::platform::BIND_PLATFORM_DEV_VID_VERISILICON},
      {BIND_PLATFORM_DEV_PID, 0, bind::fuchsia::platform::BIND_PLATFORM_DEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0,
       bind::fuchsia::verisilicon::platform::BIND_PLATFORM_DEV_DID_MAGMA_VIP},
  };

  status = device->DdkAdd(ddk::DeviceAddArgs("aml-nna").set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create aml nna device: %d\n", status);
    return status;
  }
  zxlogf(INFO, "Added aml_nna device\n");

  // intentionally leaked as it is now held by DevMgr.
  __UNUSED auto ptr = device.release();
  return status;
}

void AmlNnaDevice::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlNnaDevice::Create;
  return ops;
}();

}  // namespace aml_nna

// clang-format off
ZIRCON_DRIVER(aml_nna, aml_nna::driver_ops, "zircon", "0.1");
