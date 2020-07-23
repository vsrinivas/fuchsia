// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE

#include <zircon/syscalls.h>

#include <dev/pci/designware/dw-pcie.h>

#include "dw-pcie-hw.h"

namespace {

const uint64_t kMask32 = 0xffffffff;

inline uint32_t lo32(const uint64_t v) { return static_cast<uint32_t>(v & kMask32); }

inline uint32_t hi32(const uint64_t v) { return static_cast<uint32_t>((v >> 32)); }

}  // namespace

namespace pcie {

namespace designware {

bool DwPcie::IsLinkUp() {
  auto phyDebugR1 = PortLogic::DebugR1::Get().ReadFrom(&dbi_);

  const bool isLinkUp = phyDebugR1.link_up();
  const bool isLinkTraining = phyDebugR1.link_in_training();

  return isLinkUp && !isLinkTraining;
}

uint32_t DwPcie::ReadRC(const uint32_t offset) { return dbi_.Read32(offset); }

void DwPcie::WriteRC(const uint32_t offset, const uint32_t val) {
  return dbi_.Write32(val, offset);
}

/*
 * Program a region into the outbound ATU
 * The ATU supports 16 regions that can be programmed independently.
 *   pcie,              PCIe Device Struct
 *   index,             Which iATU region are we programming?
 *   type,              Type of PCIe txn being generated on the PCIe bus
 *   cpu_addr,          Physical source address to translate in the CPU's address space
 *   pci_addr,          Destination Address in the PCIe address space
 *   size               Size of the aperature that we're translating.
 */
zx_status_t DwPcie::ProgramOutboundAtu(const uint32_t index, const uint32_t type,
                                       const zx_paddr_t cpu_addr, const uintptr_t pci_addr,
                                       const size_t size) {
  // The ATU supports a limited number of regions.
  ZX_DEBUG_ASSERT(index < kAtuRegionCount);

  // Each ATU region has its own bank of registers at this offset from the
  // DBI base
  const size_t bank_offset = (0x3 << 20) | (index << 9);
  MMIO_PTR volatile uint8_t* atu_base =
      reinterpret_cast<MMIO_PTR volatile uint8_t*>(dbi_.get()) + bank_offset;

  MMIO_PTR volatile atu_ctrl_regs_t* regs =
      reinterpret_cast<MMIO_PTR volatile atu_ctrl_regs_t*>(atu_base);

  // Memory transactions that are in the following range will get translated
  // to PCI bus transactions:
  //
  // [cpu_addr, cpu_addr + size - 1]
  MmioWrite32(lo32(cpu_addr), &regs->unroll_lower_base);
  MmioWrite32(hi32(cpu_addr), &regs->unroll_upper_base);

  MmioWrite32(lo32(cpu_addr + size - 1), &regs->unroll_limit);

  // Target of the transactions above.
  MmioWrite32(lo32(pci_addr), &regs->unroll_lower_target);
  MmioWrite32(hi32(pci_addr), &regs->unroll_upper_target);

  // Region Ctrl 1 contains a number of fields. The Low 5 bits of the field
  // indicate the type of transaction to dispatch onto the PCIe bus.
  MmioWrite32(type, &regs->region_ctrl1);

  // Each region can individually be marked as Enabled or Disabled.
  auto temp = MmioRead32(&regs->region_ctrl2);
  temp |= kAtuRegionCtrlEnable;
  MmioWrite32(temp, &regs->region_ctrl2);
  temp |= kAtuCfgShiftMode;
  MmioWrite32(temp, &regs->region_ctrl2);

  // Wait for the enable to take effect.
  for (unsigned int i = 0; i < kAtuProgramRetries; ++i) {
    if (MmioRead32(&regs->region_ctrl2) & kAtuRegionCtrlEnable) {
      return ZX_OK;
    }

    // Wait a little bit before trying again.
    zx_nanosleep(zx_deadline_after(ZX_USEC(kAtuWaitEnableTimeoutUs)));
  }

  return ZX_ERR_TIMED_OUT;
}

void DwPcie::LinkSpeedChange() { dbi_.SetBits32(G2_CTRL_DIRECT_SPEED_CHANGE, GEN2_CTRL_OFF); }

zx_status_t DwPcie::SetupRootComplex(const iatu_translation_entry_t* cfg,
                                     const iatu_translation_entry_t* io,
                                     const iatu_translation_entry_t* mem) {
  uint32_t portLinkMode = 0;
  const uint32_t g2ctrlNoOfLanes = G2_CTRL_NO_OF_LANES(nLanes_);

  switch (nLanes_) {
    case 1:
      portLinkMode = PLC_LINK_CAPABLE_X1;
      break;
    case 2:
      portLinkMode = PLC_LINK_CAPABLE_X2;
      break;
    case 4:
      portLinkMode = PLC_LINK_CAPABLE_X4;
      break;
    case 8:
      portLinkMode = PLC_LINK_CAPABLE_X8;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  uint32_t val;
  val = ReadRC(PORT_LINK_CTRL_OFF);
  val &= ~PLC_LINK_CAPABLE_MASK;
  val |= portLinkMode;
  WriteRC(PORT_LINK_CTRL_OFF, val);

  val = ReadRC(GEN2_CTRL_OFF);
  val &= ~G2_CTRL_NUM_OF_LANES_MASK;
  val |= g2ctrlNoOfLanes;
  WriteRC(GEN2_CTRL_OFF, g2ctrlNoOfLanes);

  WriteRC(PCI_TYPE1_BAR0, 0x4);
  WriteRC(PCI_TYPE1_BAR1, 0x0);

  uint32_t idx = 0;
  if (cfg) {
    ProgramOutboundAtu(idx, PCIE_TLP_TYPE_CFG0, cfg->cpu_addr, cfg->pci_addr, cfg->length);
    idx++;
  }

  if (io) {
    ProgramOutboundAtu(idx, PCIE_TLP_TYPE_IO_RW, cfg->cpu_addr, cfg->pci_addr, cfg->length);
    idx++;
  }

  if (mem) {
    ProgramOutboundAtu(idx, PCIE_TLP_TYPE_MEM_RW, cfg->cpu_addr, cfg->pci_addr, cfg->length);
    idx++;
  }

  LinkSpeedChange();

  return ZX_OK;
}

}  // namespace designware
}  // namespace pcie
