// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ML_DRIVERS_AML_NNA_AML_NNA_H_
#define SRC_DEVICES_ML_DRIVERS_AML_NNA_AML_NNA_H_

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <fuchsia/hardware/registers/llcpp/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <zircon/fidl.h>

#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-registers.h>

namespace aml_nna {

class AmlNnaDevice;
using AmlNnaDeviceType = ddk::Device<AmlNnaDevice, ddk::GetProtocolable, ddk::Unbindable>;

class AmlNnaDevice : public AmlNnaDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_NNA> {
 public:
  // Each offset is the byte offset of the register in their respective mmio region.
  struct NnaBlock {
    // Power Domain MMIO.
    uint32_t domain_power_sleep_offset;
    uint32_t domain_power_iso_offset;
    // Set power state (1 = power off)
    uint32_t domain_power_sleep_bits;
    // Set control output signal isolation (1 = set isolation)
    uint32_t domain_power_iso_bits;

    // Memory PD MMIO.
    uint32_t hhi_mem_pd_reg0_offset;
    uint32_t hhi_mem_pd_reg1_offset;

    // Reset MMIO.
    uint32_t reset_level2_offset;

    // Hiu MMIO.
    uint32_t clock_control_offset;
  };

  explicit AmlNnaDevice(zx_device_t* parent, ddk::MmioBuffer hiu_mmio, ddk::MmioBuffer power_mmio,
                        ddk::MmioBuffer memory_pd_mmio, zx::channel reset, ddk::PDev pdev,
                        NnaBlock nna_block)
      : AmlNnaDeviceType(parent),
        pdev_(std::move(pdev)),
        hiu_mmio_(std::move(hiu_mmio)),
        power_mmio_(std::move(power_mmio)),
        memory_pd_mmio_(std::move(memory_pd_mmio)),
        reset_(std::move(reset)),
        nna_block_(nna_block) {
    pdev_.GetProto(&parent_pdev_);
  }
  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Init();

  // Methods required by the ddk.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

 private:
  ddk::PDev pdev_;
  ddk::MmioBuffer hiu_mmio_;
  ddk::MmioBuffer power_mmio_;
  ddk::MmioBuffer memory_pd_mmio_;
  ::llcpp::fuchsia::hardware::registers::Device::SyncClient reset_;

  pdev_protocol_t parent_pdev_;

  NnaBlock nna_block_;
};

}  // namespace aml_nna

#endif  // SRC_DEVICES_ML_DRIVERS_AML_NNA_AML_NNA_H_
