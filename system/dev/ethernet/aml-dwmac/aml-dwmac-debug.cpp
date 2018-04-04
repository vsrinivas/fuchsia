// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-device.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/type_support.h>

#include <fbl/ref_ptr.h>
#include <fbl/vmar_manager.h>
#include <fbl/vmo_mapper.h>
#include <hw/reg.h>
#include <pretty/hexdump.h>
#include <zircon/compiler.h>

#include <soc/aml-s912/s912-hw.h>

#include <stdio.h>
#include <string.h>

#include "aml-dwmac.h"
namespace eth {

void AmlDWMacDevice::DumpRegisters() {
    uint32_t val;
    for (uint32_t i = 0; i < 31; i++) {
        if (MDIORead(i, &val) == ZX_OK) {
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
    MDIORead(1, &temp);
    zxlogf(INFO, "MII Status = %08x\n", temp);
    MDIORead(1, &temp);
    zxlogf(INFO, "MII Status = %08x\n", temp);
}
} //namespace eth