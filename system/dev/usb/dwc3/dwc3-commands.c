// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc3.h"
#include "dwc3-regs.h"

#include <stdio.h>

static void dwc3_ep_cmd(dwc3_t* dwc, unsigned ep_num, uint32_t command, uint32_t param0,
                        uint32_t param1, uint32_t param2, uint32_t flags) {
    volatile void* mmio = dwc3_mmio(dwc);

    mtx_lock(&dwc->lock);

    DWC3_WRITE32(mmio + DEPCMDPAR0(ep_num), param0);
    DWC3_WRITE32(mmio + DEPCMDPAR1(ep_num), param1);
    DWC3_WRITE32(mmio + DEPCMDPAR2(ep_num), param2);

    command |= (DEPCMD_CMDACT | flags);
    volatile void* depcmd = mmio + DEPCMD(ep_num);
    DWC3_WRITE32(depcmd, command | DEPCMD_CMDACT);

    if ((flags & DEPCMD_CMDIOC) == 0) {
        dwc3_wait_bits(depcmd, DEPCMD_CMDACT, 0);
    }
    mtx_unlock(&dwc->lock);
}

void dwc3_cmd_start_new_config(dwc3_t* dwc, unsigned ep_num, unsigned rsrc_id) {
    dwc3_ep_cmd(dwc, ep_num, DEPSTARTCFG | DEPCMD_RESOURCE_INDEX(rsrc_id),  0, 0, 0, 0);
}

void dwc3_cmd_ep_set_config(dwc3_t* dwc, unsigned ep_num, unsigned ep_type,
                                   unsigned max_packet_size, unsigned interval, bool modify) {
    // fifo number is zero for OUT endpoints and EP0_IN
    unsigned fifo_num = (EP_OUT(ep_num) || ep_num == EP0_IN ? 0 : ep_num >> 1);
    uint32_t param0 = DEPCFG_FIFO_NUM(fifo_num) | DEPCFG_MAX_PACKET_SIZE(max_packet_size)
                      | DEPCFG_EP_TYPE(ep_type);
    if (modify) {
        param0 |= DEPCFG_ACTION_MODIFY;
    } else {
        param0 |= DEPCFG_ACTION_INITIALIZE;
    }
    uint32_t param1 = DEPCFG_EP_NUMBER(ep_num) | DEPCFG_INTERVAL(interval) |
                      DEPCFG_XFER_NOT_READY_EN | DEPCFG_XFER_COMPLETE_EN | DEPCFG_INTR_NUM(0);
    dwc3_ep_cmd(dwc, ep_num, DEPCFG, param0, param1, 0, 0);
}

void dwc3_cmd_ep_transfer_config(dwc3_t* dwc, unsigned ep_num) {
    dwc3_ep_cmd(dwc, ep_num, DEPXFERCFG, 1, 0, 0, 0);
}

void dwc3_cmd_ep_start_transfer(dwc3_t* dwc, unsigned ep_num, zx_paddr_t trb_phys) {
    dwc3_ep_cmd(dwc, ep_num, DEPSTRTXFER, (uint32_t)(trb_phys >> 32),
                (uint32_t)trb_phys, 0, DEPCMD_CMDIOC);
}

void dwc3_cmd_ep_end_transfer(dwc3_t* dwc, unsigned ep_num) {
    unsigned rsrc_id = dwc->eps[ep_num].rsrc_id;
    dwc3_ep_cmd(dwc, ep_num, DEPENDXFER, 0, 0, 0,
                DEPCMD_RESOURCE_INDEX(rsrc_id) | DEPCMD_CMDIOC | DEPCMD_HIPRI_FORCERM);
}

void dwc3_cmd_ep_set_stall(dwc3_t* dwc, unsigned ep_num) {
    dwc3_ep_cmd(dwc, ep_num, DEPSSTALL, 0, 0, 0, DEPCMD_CMDIOC);
}

void dwc3_cmd_ep_clear_stall(dwc3_t* dwc, unsigned ep_num) {
    dwc3_ep_cmd(dwc, ep_num, DEPCSTALL, 0, 0, 0, DEPCMD_CMDIOC);
}
