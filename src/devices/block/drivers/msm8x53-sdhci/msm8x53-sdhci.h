// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_MSM8X53_SDHCI_MSM8X53_SDHCI_H_
#define SRC_DEVICES_BLOCK_DRIVERS_MSM8X53_SDHCI_MSM8X53_SDHCI_H_

#include <fuchsia/hardware/sdhci/cpp/banjo.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>

namespace sdhci {

class Msm8x53Sdhci;
using DeviceType = ddk::Device<Msm8x53Sdhci>;

class Msm8x53Sdhci : public DeviceType,
                     public ddk::SdhciProtocol<Msm8x53Sdhci, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  virtual ~Msm8x53Sdhci() = default;

  void DdkRelease() { delete this; }

  zx_status_t Init();

  zx_status_t SdhciGetInterrupt(zx::interrupt* out_irq);
  zx_status_t SdhciGetMmio(zx::vmo* out_mmio, zx_off_t* out_offset);
  zx_status_t SdhciGetBti(uint32_t index, zx::bti* out_bti);
  uint32_t SdhciGetBaseClock();
  uint64_t SdhciGetQuirks(uint64_t* out_dma_boundary_alignment);
  void SdhciHwReset();

 private:
  Msm8x53Sdhci(zx_device_t* parent, ddk::MmioBuffer core_mmio, ddk::MmioBuffer hc_mmio,
               zx::interrupt irq)
      : DeviceType(parent),
        core_mmio_(std::move(core_mmio)),
        hc_mmio_(std::move(hc_mmio)),
        irq_(std::move(irq)) {}

  int IrqThread();

  ddk::MmioBuffer core_mmio_;
  ddk::MmioBuffer hc_mmio_;
  zx::interrupt irq_;
};

}  // namespace sdhci

#endif  // SRC_DEVICES_BLOCK_DRIVERS_MSM8X53_SDHCI_MSM8X53_SDHCI_H_
