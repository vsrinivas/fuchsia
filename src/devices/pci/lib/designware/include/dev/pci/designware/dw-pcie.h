// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PCI_LIB_DESIGNWARE_INCLUDE_DEV_PCI_DESIGNWARE_DW_PCIE_H_
#define SRC_DEVICES_PCI_LIB_DESIGNWARE_INCLUDE_DEV_PCI_DESIGNWARE_DW_PCIE_H_

#include <lib/mmio/mmio.h>
#include <stdint.h>
#include <zircon/types.h>

#include <utility>

#include <dev/pci/designware/atu-cfg.h>

namespace pcie {
namespace designware {

class DwPcie {
 protected:
  DwPcie(fdf::MmioBuffer dbi, fdf::MmioBuffer cfg, const uint32_t nLanes)
      : dbi_(std::move(dbi)), cfg_(std::move(cfg)), nLanes_(nLanes) {}
  virtual ~DwPcie() {}

  virtual bool IsLinkUp();

  virtual uint32_t ReadRC(const uint32_t offset);
  virtual void WriteRC(const uint32_t offset, const uint32_t val);

  zx_status_t SetupRootComplex(const iatu_translation_entry_t* cfg,
                               const iatu_translation_entry_t* io,
                               const iatu_translation_entry_t* mem);

  fdf::MmioBuffer dbi_;
  fdf::MmioBuffer cfg_;
  const uint32_t nLanes_;

 private:
  void LinkSpeedChange();
  zx_status_t ProgramOutboundAtu(const uint32_t index, const uint32_t type,
                                 const zx_paddr_t cpu_addr, const uintptr_t pci_addr,
                                 const size_t sz);
};

}  // namespace designware
}  // namespace pcie

#endif  // SRC_DEVICES_PCI_LIB_DESIGNWARE_INCLUDE_DEV_PCI_DESIGNWARE_DW_PCIE_H_
