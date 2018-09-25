// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <ddk/debug.h>
#include <dev/pci/designware/atu-cfg.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>

#include "aml-pcie-device.h"
#include "aml-pcie-regs.h"
#include "aml-pcie.h"

namespace pcie {
namespace aml {

void AmlPcie::AssertReset(const uint32_t mask) {
    rst_->ClearBits32(mask, 0);
}

void AmlPcie::ClearReset(const uint32_t mask) {
    rst_->SetBits32(mask, 0);
}

void AmlPcie::RmwCtrlSts(const uint32_t size, const uint32_t shift, const uint32_t mask) {
    uint32_t regval;
    switch (size) {
    case 128:
        regval = 0;
        break;
    case 256:
        regval = 1;
        break;
    case 512:
        regval = 2;
        break;
    case 1024:
        regval = 3;
        break;
    case 2048:
        regval = 4;
        break;
    case 4096:
        regval = 5;
        break;
    default:
        regval = 1;
    }

    dbi_->ClearBits32(mask << shift, PCIE_CTRL_STS_OFF);
    dbi_->SetBits32(regval << shift, PCIE_CTRL_STS_OFF);
}

void AmlPcie::PcieInit() {
    cfg_->SetBits32(APP_LTSSM_ENABLE, 0);

    dbi_->SetBits32(PLC_FAST_LINK_MODE, PORT_LINK_CTRL_OFF);

    dbi_->ClearBits32(PLC_LINK_CAPABLE_MASK, PORT_LINK_CTRL_OFF);

    dbi_->SetBits32(PLC_LINK_CAPABLE_X1, PORT_LINK_CTRL_OFF);

    dbi_->ClearBits32(G2_CTRL_NUM_OF_LANES_MASK, GEN2_CTRL_OFF);

    dbi_->SetBits32(G2_CTRL_NO_OF_LANES(1), GEN2_CTRL_OFF);

    dbi_->SetBits32(G2_CTRL_DIRECT_SPEED_CHANGE, GEN2_CTRL_OFF);
}

void AmlPcie::SetMaxPayload(const uint32_t size) {
    const uint32_t kShift = 5;
    const uint32_t kMask = 0x7;
    RmwCtrlSts(size, kShift, kMask);
}

void AmlPcie::SetMaxReadRequest(const uint32_t size) {
    const uint32_t kShift = 12;
    const uint32_t kMask = 0x7;
    RmwCtrlSts(size, kShift, kMask);
}

void AmlPcie::EnableMemorySpace() {
    // Cause the root port to handle transactions.
    constexpr uint32_t bits = (PCIE_TYPE1_STS_CMD_IO_ENABLE |
                               PCIE_TYPE1_STS_CMD_MEM_SPACE_ENABLE |
                               PCIE_TYPE1_STS_CMD_BUS_MASTER_ENABLE);
    dbi_->SetBits32(bits, PCIE_TYPE1_STS_CMD_OFF);
}

bool AmlPcie::IsLinkUp() {
    uint32_t val = cfg_->Read32(PCIE_CFG_STATUS12);

    return (val & PCIE_CFG12_SMLH_UP) &&
           (val & PCIE_CFG12_RDLH_UP) &&
           ((val & PCIE_CFG12_LTSSM_MASK) == PCIE_CFG12_LTSSM_UP);
}

zx_status_t AmlPcie::AwaitLinkUp() {
    for (unsigned int i = 0; i < 500000; i++) {
        if (IsLinkUp()) {
            zxlogf(SPEW, "aml_dw: pcie link up okay\n");
            return ZX_OK;
        }
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    }
    return ZX_ERR_TIMED_OUT;
}

void AmlPcie::ConfigureRootBridge() {
    // PCIe Type 1 header Bus Register (offset 0x18 into the Ecam)
    auto reg = PciBusReg::Get().ReadFrom(dbi_.get());

    // The Upstream Bus for the root bridge is Bus 0
    reg.set_primary_bus(0x0);

    // The Downstream bus for the root bridge is Bus 1
    reg.set_secondary_bus(0x1);

    // This bridge will also claim all transactions for any other bus IDs on
    // this bus.
    reg.set_subordinate_bus(0x1);

    reg.WriteTo(dbi_.get());

    // Zero out the BARs for the Root bridge because the DW root bridge doesn't
    // need them.
    dbi_->Write32(0, PCI_TYPE1_BAR0);
    dbi_->Write32(0, PCI_TYPE1_BAR1);
}

zx_status_t AmlPcie::EstablishLink(const iatu_translation_entry_t* cfg,
                                   const iatu_translation_entry_t* io,
                                   const iatu_translation_entry_t* mem) {
    zx_status_t st;

    PcieInit();

    SetMaxPayload(256);

    SetMaxReadRequest(256);

    st = SetupRootComplex(cfg, io, mem);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to setup root complex, st = %d\n", st);
        return st;
    }

    EnableMemorySpace();

    st = AwaitLinkUp();
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed awaiting link up, st = %d\n", st);
        return st;
    }

    ConfigureRootBridge();

    return ZX_OK;
}

}  // namespace aml
}  // namespace pcie
