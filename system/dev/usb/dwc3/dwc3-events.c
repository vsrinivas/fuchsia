// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <hw/arch_ops.h>

#include "dwc3.h"
#include "dwc3-regs.h"

#include <stdio.h>
#include <unistd.h>

static void dwc3_handle_ep_event(dwc3_t* dwc, uint32_t event) {
    uint32_t type = DEPEVT_TYPE(event);
    uint32_t ep_num = DEPEVT_PHYS_EP(event);
    uint32_t status = DEPEVT_STATUS(event);

    switch (type) {
    case DEPEVT_XFER_COMPLETE:
        dwc3_ep_xfer_complete(dwc, ep_num);
        break;
    case DEPEVT_XFER_IN_PROGRESS:
        dprintf(TRACE, "DEPEVT_XFER_IN_PROGRESS ep_num: %u status %u\n", ep_num, status);
        break;
    case DEPEVT_XFER_NOT_READY:
        dwc3_ep_xfer_not_ready(dwc, ep_num, DEPEVT_XFER_NOT_READY_STAGE(event));
        break;
    case DEPEVT_STREAM_EVT:
        dprintf(TRACE, "DEPEVT_STREAM_EVT ep_num: %u status %u\n", ep_num, status);
        break;
    case DEPEVT_CMD_CMPLT: {
        unsigned cmd_type = DEPEVT_CMD_CMPLT_CMD_TYPE(event);
        unsigned rsrc_id = DEPEVT_CMD_CMPLT_RSRC_ID(event);
        if (cmd_type == DEPSTRTXFER) {
            dwc3_ep_xfer_started(dwc, ep_num, rsrc_id);
        }
        break;
    }
    default:
        dprintf(ERROR, "dwc3_handle_ep_event: unknown event type %u\n", type);
        break;
    }
}

static void dwc3_handle_event(dwc3_t* dwc, uint32_t event) {
    dprintf(LTRACE, "dwc3_handle_event %08X\n", event);
    if (!(event & DEPEVT_NON_EP)) {
        dwc3_handle_ep_event(dwc, event);
        return;
    }

    uint32_t type = DEVT_TYPE(event);
    uint32_t info = DEVT_INFO(event);

    switch (type) {
    case DEVT_DISCONNECT:
        dprintf(TRACE, "DEVT_DISCONNECT\n");
        break;
    case DEVT_USB_RESET:
        dprintf(TRACE, "DEVT_USB_RESET\n");
        dwc3_usb_reset(dwc);
        break;
    case DEVT_CONNECTION_DONE:
        dprintf(TRACE, "DEVT_CONNECTION_DONE\n");
        dwc3_connection_done(dwc);
        break;
    case DEVT_LINK_STATE_CHANGE:
        dprintf(TRACE, "DEVT_LINK_STATE_CHANGE: ");
        switch (info) {
        case DSTS_USBLNKST_U0 | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_U0\n");
            break;
        case DSTS_USBLNKST_U1 | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_U1\n");
            break;
        case DSTS_USBLNKST_U2 | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_U2\n");
            break;
        case DSTS_USBLNKST_U3 | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_U3\n");
            break;
        case DSTS_USBLNKST_ESS_DIS | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_ESS_DIS\n");
            break;
        case DSTS_USBLNKST_RX_DET | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_RX_DET\n");
            break;
        case DSTS_USBLNKST_ESS_INACT | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_ESS_INACT\n");
            break;
        case DSTS_USBLNKST_POLL | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_POLL\n");
            break;
        case DSTS_USBLNKST_RECOV | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_RECOV\n");
            break;
        case DSTS_USBLNKST_HRESET | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_HRESET\n");
            break;
        case DSTS_USBLNKST_CMPLY | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_CMPLY\n");
            break;
        case DSTS_USBLNKST_LPBK | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_LPBK\n");
            break;
        case DSTS_USBLNKST_RESUME_RESET | DEVT_LINK_STATE_CHANGE_SS:
            dprintf(TRACE, "DSTS_USBLNKST_RESUME_RESET\n");
            break;
        case DSTS_USBLNKST_ON:
            dprintf(TRACE, "DSTS_USBLNKST_ON\n");
            break;
        case DSTS_USBLNKST_SLEEP:
            dprintf(TRACE, "DSTS_USBLNKST_SLEEP\n");
            break;
        case DSTS_USBLNKST_SUSPEND:
            dprintf(TRACE, "DSTS_USBLNKST_SUSPEND\n");
            break;
        case DSTS_USBLNKST_DISCONNECTED:
            dprintf(TRACE, "DSTS_USBLNKST_DISCONNECTED\n");
            break;
        case DSTS_USBLNKST_EARLY_SUSPEND:
            dprintf(TRACE, "DSTS_USBLNKST_EARLY_SUSPEND\n");
            break;
        case DSTS_USBLNKST_RESET:
            dprintf(TRACE, "DSTS_USBLNKST_RESET\n");
            break;
        case DSTS_USBLNKST_RESUME:
            dprintf(TRACE, "DSTS_USBLNKST_RESUME\n");
            break;
        default:
            dprintf(ERROR, "unknown state %d\n", info);
            break;
        }
        break;
    case DEVT_REMOTE_WAKEUP:
        dprintf(TRACE, "DEVT_REMOTE_WAKEUP\n");
        break;
    case DEVT_HIBERNATE_REQUEST:
        dprintf(TRACE, "DEVT_HIBERNATE_REQUEST\n");
        break;
    case DEVT_SUSPEND_ENTRY:
        dprintf(TRACE, "DEVT_SUSPEND_ENTRY\n");
        //TODO(voydanoff) is this the best way to detect disconnect?
        dwc3_disconnected(dwc);
        break;
    case DEVT_SOF:
        dprintf(TRACE, "DEVT_SOF\n");
        break;
    case DEVT_ERRATIC_ERROR:
        dprintf(TRACE, "DEVT_ERRATIC_ERROR\n");
        break;
    case DEVT_COMMAND_COMPLETE:
        dprintf(TRACE, "DEVT_COMMAND_COMPLETE\n");
        break;
    case DEVT_EVENT_BUF_OVERFLOW:
        dprintf(TRACE, "DEVT_EVENT_BUF_OVERFLOW\n");
        break;
    case DEVT_VENDOR_TEST_LMP:
        dprintf(TRACE, "DEVT_VENDOR_TEST_LMP\n");
        break;
    case DEVT_STOPPED_DISCONNECT:
        dprintf(TRACE, "DEVT_STOPPED_DISCONNECT\n");
        break;
    case DEVT_L1_RESUME_DETECT:
        dprintf(TRACE, "DEVT_L1_RESUME_DETECT\n");
        break;
    case DEVT_LDM_RESPONSE:
        dprintf(TRACE, "DEVT_LDM_RESPONSE\n");
        break;
    default:
        dprintf(ERROR, "dwc3_handle_event: unknown event type %u\n", type);
        break;
    }
}

static int dwc3_irq_thread(void* arg) {
    dwc3_t* dwc = arg;
    volatile void* mmio = dwc3_mmio(dwc);

    dprintf(TRACE, "dwc3_irq_thread start\n");

    uint32_t* ring_start = io_buffer_virt(&dwc->event_buffer);
    uint32_t* ring_end = (void *)ring_start + EVENT_BUFFER_SIZE;
    volatile uint32_t* ring_cur = ring_start;

    while (1) {
        mx_status_t status = mx_interrupt_wait(dwc->irq_handle);
        mx_interrupt_complete(dwc->irq_handle);
        if (status != MX_OK) {
            dprintf(ERROR, "dwc3_irq_thread: mx_interrupt_wait returned %d\n", status);
            break;
        }

        // read number of new bytes in the event buffer
        uint32_t event_count;
        while ((event_count = DWC3_READ32(mmio + GEVNTCOUNT(0)) & GEVNTCOUNT_EVNTCOUNT_MASK) > 0) {
            // invalidate cache so we can read fresh events
            io_buffer_cache_op(&dwc->event_buffer, MX_VMO_OP_CACHE_INVALIDATE, 0,
                               EVENT_BUFFER_SIZE);

            for (unsigned i = 0; i < event_count; i += sizeof(uint32_t)) {
                uint32_t event = *ring_cur++;
                if (ring_cur == ring_end) {
                    ring_cur = ring_start;
                }
                dwc3_handle_event(dwc, event);
            }

            // acknowledge the events we have processed
            DWC3_WRITE32(mmio + GEVNTCOUNT(0), event_count);
        }
    }

    dprintf(TRACE, "dwc3_irq_thread done\n");
    return 0;
}

void dwc3_events_start(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);

    // set event buffer pointer and size
    // keep interrupts masked until we are ready
    mx_paddr_t paddr = io_buffer_phys(&dwc->event_buffer);
    DWC3_WRITE32(mmio + GEVNTADRLO(0), (uint32_t)paddr);
    DWC3_WRITE32(mmio + GEVNTADRHI(0), (uint32_t)(paddr >> 32));
    DWC3_WRITE32(mmio + GEVNTSIZ(0), EVENT_BUFFER_SIZE | GEVNTSIZ_EVNTINTRPTMASK);
    DWC3_WRITE32(mmio + GEVNTCOUNT(0), 0);

    // enable events
    uint32_t event_mask = DEVTEN_USBRSTEVTEN | DEVTEN_CONNECTDONEEVTEN | DEVTEN_DISSCONNEVTEN |
                          DEVTEN_L1SUSPEN | DEVTEN_U3_L2_SUSP_EN;
    DWC3_WRITE32(mmio + DEVTEN, event_mask);

    thrd_create_with_name(&dwc->irq_thread, dwc3_irq_thread, dwc, "dwc3_irq_thread");
}
