// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <hw/arch_ops.h>
#include <stdio.h>

#include "xhci-util.h"

static void xhci_sync_command_callback(void* data, uint32_t cc, xhci_trb_t* command_trb,
                                         xhci_trb_t* event_trb) {
    xhci_sync_command_t* command = (xhci_sync_command_t*)data;
    command->status = XHCI_READ32(&event_trb->status);
    command->control = XHCI_READ32(&event_trb->control);
    completion_signal(&command->completion);
}

void xhci_sync_command_init(xhci_sync_command_t* command) {
    completion_reset(&command->completion);
    command->context.callback = xhci_sync_command_callback;
    command->context.data = command;
}

// returns condition code
int xhci_sync_command_wait(xhci_sync_command_t* command) {
    completion_wait(&command->completion, ZX_TIME_INFINITE);

    return (command->status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
}

zx_status_t xhci_send_command(xhci_t* xhci, uint32_t cmd, uint64_t ptr, uint32_t control_bits) {
    xhci_sync_command_t command;
    int cc;

    xhci_sync_command_init(&command);
    xhci_post_command(xhci, cmd, ptr, control_bits, &command.context);

    // Wait for one second (arbitrarily chosen timeout)
    // TODO(voydanoff) consider making the timeout a parameter to this function
    zx_status_t status = completion_wait(&command.completion, ZX_SEC(1));
    if (status == ZX_OK) {
        cc = (command.status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
         if (cc == TRB_CC_SUCCESS) {
            return ZX_OK;
        }
        zxlogf(ERROR, "xhci_send_command %u failed, cc: %d\n", cmd, cc);
        return ZX_ERR_INTERNAL;
    } else if (status == ZX_ERR_TIMED_OUT) {
        completion_reset(&command.completion);

        // abort the command
        volatile uint64_t* crcr_ptr = &xhci->op_regs->crcr;
        XHCI_WRITE64(crcr_ptr, CRCR_CA);

        // wait for TRB_CC_COMMAND_ABORTED
        completion_wait(&command.completion, ZX_TIME_INFINITE);
        cc = (command.status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
        if (cc == TRB_CC_SUCCESS) {
            // command must have completed while we were trying to abort it
            status = ZX_OK;
        }

        // ring doorbell to restart command ring
        hw_mb();
        XHCI_WRITE32(&xhci->doorbells[0], 0);
        xhci_wait_bits64(crcr_ptr, CRCR_CRR, CRCR_CRR);
    }

    return status;
}

uint32_t* xhci_get_next_ext_cap(void* mmio, uint32_t* prev_cap, uint32_t* match_cap_id) {
    uint32_t* cap_ptr = prev_cap;
    if (!cap_ptr) {
        // Find the first cap.
        xhci_cap_regs_t* xhci_cap_regs = (xhci_cap_regs_t*)mmio;
        volatile uint32_t* hccparams1 = &xhci_cap_regs->hccparams1;

        uint32_t offset = XHCI_GET_BITS32(hccparams1, HCCPARAMS1_EXT_CAP_PTR_START,
                                          HCCPARAMS1_EXT_CAP_PTR_BITS);
        if (!offset) {
            return NULL;
        }
        // offset is 32-bit words from MMIO base
        cap_ptr = (uint32_t *)(mmio + (offset << 2));
    }

    while (cap_ptr) {
        // We only want to check the current cap for a match if it's not the
        // one the user gave us.
        if (cap_ptr != prev_cap) {
            uint32_t cap_id = XHCI_GET_BITS32(cap_ptr, EXT_CAP_CAPABILITY_ID_START,
                                              EXT_CAP_CAPABILITY_ID_BITS);

            // The cap only matches if the user didn't specify an id to match,
            // or the ids are equal.
            if (!match_cap_id || (*match_cap_id == cap_id)) {
                return cap_ptr;
            }
        }
        // Get the next cap ptr, offset is 32-bit words from cap_ptr
        uint32_t offset = XHCI_GET_BITS32(cap_ptr, EXT_CAP_NEXT_PTR_START,
                                          EXT_CAP_NEXT_PTR_BITS);
        cap_ptr = (offset ? cap_ptr + offset : NULL);
    }
    return NULL;
}
