// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_REGS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_REGS_H_

// This file contains register definitions used by the Broadcom chip on the PCIE bus.

#include <zircon/types.h>

namespace wlan {
namespace brcmfmac {

constexpr uint32_t BRCMF_PCIE_REG_MAP_SIZE = (32 * 1024);
constexpr uint32_t BRCMF_PCIE_BAR0_WINDOW = 0x80;
constexpr uint32_t BRCMF_PCIE_BAR0_REG_SIZE = 0x1000;

constexpr uint32_t BRCMF_PCIE_ARMCR4REG_BANKIDX = 0x40;
constexpr uint32_t BRCMF_PCIE_ARMCR4REG_BANKPDA = 0x4C;

constexpr uint32_t BRCMF_PCIE_REG_LINK_STATUS_CTRL = 0xBC;

constexpr uint32_t BRCMF_PCIE_PCIE2REG_INTMASK = 0x24;
constexpr uint32_t BRCMF_PCIE_PCIE2REG_MAILBOXINT = 0x48;
constexpr uint32_t BRCMF_PCIE_PCIE2REG_MAILBOXMASK = 0x4C;
constexpr uint32_t BRCMF_PCIE_PCIE2REG_CONFIGADDR = 0x120;
constexpr uint32_t BRCMF_PCIE_PCIE2REG_CONFIGDATA = 0x124;
constexpr uint32_t BRCMF_PCIE_PCIE2REG_H2D_MAILBOX = 0x140;

constexpr uint32_t BRCMF_PCIE_CFGREG_STATUS_CMD = 0x4;
constexpr uint32_t BRCMF_PCIE_CFGREG_PM_CSR = 0x4C;
constexpr uint32_t BRCMF_PCIE_CFGREG_MSI_CAP = 0x58;
constexpr uint32_t BRCMF_PCIE_CFGREG_MSI_ADDR_L = 0x5C;
constexpr uint32_t BRCMF_PCIE_CFGREG_MSI_ADDR_H = 0x60;
constexpr uint32_t BRCMF_PCIE_CFGREG_MSI_DATA = 0x64;
constexpr uint32_t BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL = 0xBC;
constexpr uint32_t BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL2 = 0xDC;
constexpr uint32_t BRCMF_PCIE_CFGREG_RBAR_CTRL = 0x228;
constexpr uint32_t BRCMF_PCIE_CFGREG_PML1_SUB_CTRL1 = 0x248;
constexpr uint32_t BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG = 0x4E0;
constexpr uint32_t BRCMF_PCIE_CFGREG_REG_BAR3_CONFIG = 0x4F4;
constexpr uint32_t BRCMF_PCIE_LINK_STATUS_CTRL_ASPM_ENAB = 3;

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_REGS_H_
