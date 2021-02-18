// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_PCI_EXPRESS_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_PCI_EXPRESS_H_

#include "src/devices/bus/drivers/pci/capabilities.h"

namespace pci {

class PciExpressCapability : public Capability {
 public:
  PciExpressCapability(const Config& cfg, uint8_t base)
      : Capability(static_cast<uint8_t>(Capability::Id::kPciExpress), base),
        pcie_capabilities_(PciReg16(static_cast<uint16_t>(base + 0x2))),
        device_capabilities_(PciReg32(static_cast<uint16_t>(base + 0x4))),
        device_control_(PciReg16(static_cast<uint16_t>(base + 0x8))),
        device_status_(PciReg16(static_cast<uint16_t>(base + 0xA))) {}

 private:
  const PciReg16 pcie_capabilities_;
  const PciReg32 device_capabilities_;
  const PciReg16 device_control_;
  const PciReg16 device_status_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_PCI_EXPRESS_H_
