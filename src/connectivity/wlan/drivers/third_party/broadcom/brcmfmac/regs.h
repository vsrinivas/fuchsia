// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_REGS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_REGS_H_

// This file contains common register definitions used by the Broadcom chip.

#include <zircon/types.h>

namespace wlan {
namespace brcmfmac {

constexpr uint32_t BRCMF_PCIE_MB_INT_FN0_0 = 0x0100;
constexpr uint32_t BRCMF_PCIE_MB_INT_FN0_1 = 0x0200;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H0_DB0 = 0x10000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H0_DB1 = 0x20000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H1_DB0 = 0x40000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H1_DB1 = 0x80000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H2_DB0 = 0x100000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H2_DB1 = 0x200000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H3_DB0 = 0x400000;
constexpr uint32_t BRCMF_PCIE_MB_INT_D2H3_DB1 = 0x800000;

constexpr uint32_t BRCMF_PCIE_MB_INT_D2H_DB =
    BRCMF_PCIE_MB_INT_D2H0_DB0 | BRCMF_PCIE_MB_INT_D2H0_DB1 | BRCMF_PCIE_MB_INT_D2H1_DB0 |
    BRCMF_PCIE_MB_INT_D2H1_DB1 | BRCMF_PCIE_MB_INT_D2H2_DB0 | BRCMF_PCIE_MB_INT_D2H2_DB1 |
    BRCMF_PCIE_MB_INT_D2H3_DB0 | BRCMF_PCIE_MB_INT_D2H3_DB1;

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_REGS_H_
