// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
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
    completion_wait(&command->completion, MX_TIME_INFINITE);

    return (command->status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
}

mx_status_t xhci_send_command(xhci_t* xhci, uint32_t cmd, uint64_t ptr, uint32_t control_bits) {
    xhci_sync_command_t command;
    int cc;

    xhci_sync_command_init(&command);
    xhci_post_command(xhci, cmd, ptr, control_bits, &command.context);

    // Wait for one second (arbitrarily chosen timeout)
    // TODO(voydanoff) consider making the timeout a parameter to this function
    mx_status_t status = completion_wait(&command.completion, MX_SEC(1));
    if (status == MX_OK) {
        cc = (command.status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
         if (cc == TRB_CC_SUCCESS) {
            return MX_OK;
        }
        dprintf(ERROR, "xhci_send_command %u failed, cc: %d\n", cmd, cc);
        return MX_ERR_INTERNAL;
    } else if (status == MX_ERR_TIMED_OUT) {
        completion_reset(&command.completion);

        // abort the command
        volatile uint64_t* crcr_ptr = &xhci->op_regs->crcr;
        XHCI_WRITE64(crcr_ptr, CRCR_CA);

        // wait for TRB_CC_COMMAND_ABORTED
        completion_wait(&command.completion, MX_TIME_INFINITE);
        cc = (command.status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
        if (cc == TRB_CC_SUCCESS) {
            // command must have completed while we were trying to abort it
            status = MX_OK;
        }

        // ring doorbell to restart command ring
        XHCI_WRITE32(&xhci->doorbells[0], 0);
        xhci_wait_bits64(crcr_ptr, CRCR_CRR, CRCR_CRR);
    }

    return status;
}
