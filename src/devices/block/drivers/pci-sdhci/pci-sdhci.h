// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_PCI_SDHCI_PCI_SDHCI_H_
#define SRC_DEVICES_BLOCK_DRIVERS_PCI_SDHCI_PCI_SDHCI_H_

#include <fuchsia/hardware/sdhci/cpp/banjo.h>
#include <lib/device-protocol/pci.h>

#include <optional>

#include <ddktl/device.h>

namespace sdhci {

class PciSdhci;
using DeviceType = ddk::Device<PciSdhci>;

class PciSdhci : public DeviceType, public ddk::SdhciProtocol<PciSdhci, ddk::base_protocol> {
 public:
  explicit PciSdhci(zx_device_t*);

  static zx_status_t Bind(void*, zx_device_t* parent);

  zx_status_t SdhciGetInterrupt(zx::interrupt* interrupt_out);
  zx_status_t SdhciGetMmio(zx::vmo* out, zx_off_t* out_offset);
  zx_status_t SdhciGetBti(uint32_t index, zx::bti* out_bti);
  uint32_t SdhciGetBaseClock();
  uint64_t SdhciGetQuirks(uint64_t* out_dma_boundary_alignment);
  void SdhciHwReset();

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  ddk::Pci pci_;

  std::optional<fdf::MmioBuffer> mmio_;
  zx::bti bti_;
};

}  // namespace sdhci

#endif  // SRC_DEVICES_BLOCK_DRIVERS_PCI_SDHCI_PCI_SDHCI_H_
