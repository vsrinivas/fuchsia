// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwmac.h"

namespace eth {

void DWMacDevice::DumpRegisters() {
    uint32_t val;
    for (uint32_t i = 0; i < 31; i++) {
        if (EthMacMdioRead(i, &val) == ZX_OK) {
            zxlogf(INFO, "MII%02u = %08x\n", i, val);
        } else {
            zxlogf(INFO, "MDIO READ TIMEOUT%u\n", i);
        }
    }
    zxlogf(INFO, "mac addr hi -> %08x\n", dwmac_regs_->macaddr0hi);
    zxlogf(INFO, "mac addr lo -> %08x\n", dwmac_regs_->macaddr0lo);
    zxlogf(INFO, "mac version -> %08x\n", dwmac_regs_->version);
    zxlogf(INFO, "\ndma hwfeature -> %08x\n", dwdma_regs_->hwfeature);
    zxlogf(INFO, "dma busmode   -> %08x\n", dwdma_regs_->busmode);
    zxlogf(INFO, "dma status    -> %08x\n", dwdma_regs_->status);
    uint32_t temp;
    EthMacMdioRead(1, &temp);
    zxlogf(INFO, "MII Status = %08x\n", temp);
    EthMacMdioRead(1, &temp);
    zxlogf(INFO, "MII Status = %08x\n", temp);
}

void DWMacDevice::DumpStatus(uint32_t status) {
    uint32_t tx_state = (status >> 20) & 0x07;
    uint32_t rx_state = (status >> 17) & 0x07;

    zxlogf(INFO, "TX:%3u RX:%3u ---%s %s %s %s %s %s %s %s %s\n", tx_state, rx_state,
        status & (1 << 13) ? "FBI" : " ",
        status & (1 << 10) ? "ETI" : " ",
        status & (1 <<  9) ? "RWT" : " ",
        status & (1 <<  8) ? "RPS" : " ",
        status & (1 <<  7) ? "RBU" : " ",
        status & (1 <<  5) ? "TBU" : " ",
        status & (1 <<  4) ? "RBO" : " ",
        status & (1 <<  3) ? "TJT" : " ",
        status & (1 <<  1) ? "TPS" : " ");
}
} //namespace eth
