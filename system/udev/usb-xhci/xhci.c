// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "xhci.h"

//#define TRACE 1
#include "xhci-debug.h"

uint8_t xhci_endpoint_index(uint8_t ep_address) {
    if (ep_address == 0) return 0;
    uint32_t index = 2 * (ep_address & ~USB_ENDPOINT_DIR_MASK);
    if ((ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_OUT)
        index--;
    return index;
}

mx_status_t xhci_init(xhci_t* xhci, void* mmio) {
    mx_status_t result = NO_ERROR;

    list_initialize(&xhci->command_queue);

    xhci->cap_regs = (xhci_cap_regs_t*)mmio;
    xhci->op_regs = (xhci_op_regs_t*)((uint8_t*)xhci->cap_regs + xhci->cap_regs->length);
    xhci->doorbells = (uint32_t*)((uint8_t*)xhci->cap_regs + xhci->cap_regs->dboff);
    xhci->runtime_regs = (xhci_runtime_regs_t*)((uint8_t*)xhci->cap_regs + xhci->cap_regs->rtsoff);
    volatile uint32_t* hcsparams1 = &xhci->cap_regs->hcsparams1;
    volatile uint32_t* hcsparams2 = &xhci->cap_regs->hcsparams2;
    volatile uint32_t* hccparams1 = &xhci->cap_regs->hccparams1;

    xhci->max_slots = XHCI_GET_BITS32(hcsparams1, HCSPARAMS1_MAX_SLOTS_START,
                                      HCSPARAMS1_MAX_SLOTS_BITS);
    xhci->max_interruptors = XHCI_GET_BITS32(hcsparams1, HCSPARAMS1_MAX_INTRS_START,
                                             HCSPARAMS1_MAX_INTRS_BITS);
    xhci->rh_num_ports = XHCI_GET_BITS32(hcsparams1, HCSPARAMS1_MAX_PORTS_START,
                                         HCSPARAMS1_MAX_PORTS_BITS);
    xhci->context_size = (XHCI_READ32(hccparams1) & HCCPARAMS1_CSZ ? 64 : 32);

    uint32_t scratch_pad_bufs = XHCI_GET_BITS32(hcsparams2, HCSPARAMS2_MAX_SBBUF_HI_START,
                                                HCSPARAMS2_MAX_SBBUF_HI_BITS);

    // allocate array to hold our slots
    // add 1 to allow 1-based indexing of slots
    xhci->slots = (xhci_slot_t*)calloc(xhci->max_slots + 1, sizeof(xhci_slot_t));
    if (!xhci->slots) {
        result = ERR_NO_MEMORY;
        goto fail;
    }

    // Allocate DMA memory for various things
    xhci->dcbaa = xhci_memalign(xhci, 64, (xhci->max_slots + 1) * sizeof(uint64_t));
    if (!xhci->dcbaa) {
        result = ERR_NO_MEMORY;
        goto fail;
    }

    scratch_pad_bufs <<= HCSPARAMS2_MAX_SBBUF_LO_BITS;
    scratch_pad_bufs |= XHCI_GET_BITS32(hcsparams2, HCSPARAMS2_MAX_SBBUF_LO_START,
                                        HCSPARAMS2_MAX_SBBUF_LO_BITS);

    if (scratch_pad_bufs > 0) {
        xhci->scratch_pad = xhci_memalign(xhci, 64, scratch_pad_bufs * sizeof(uint64_t));
        if (!xhci->scratch_pad) {
            result = ERR_NO_MEMORY;
            goto fail;
        }
        uint32_t page_size = XHCI_READ32(&xhci->op_regs->pagesize) << 12;

        for (uint32_t i = 0; i < scratch_pad_bufs; i++) {
            void* page = xhci_memalign(xhci, page_size, page_size);
            if (!page) {
                result = ERR_NO_MEMORY;
                goto fail;
            }
            xhci->scratch_pad[i] = xhci_virt_to_phys(xhci, (mx_vaddr_t)page);
        }
        xhci->dcbaa[0] = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->scratch_pad);
    }

    result = xhci_transfer_ring_init(xhci, &xhci->command_ring, COMMAND_RING_SIZE);
    if (result != NO_ERROR) {
        printf("xhci_command_ring_init failed\n");
        goto fail;
    }
    result = xhci_event_ring_init(xhci, 0, EVENT_RING_SIZE);
    if (result != NO_ERROR) {
        printf("xhci_event_ring_init failed\n");
        goto fail;
    }

    return NO_ERROR;

fail:
    xhci_event_ring_free(xhci, 0);
    xhci_transfer_ring_free(xhci, &xhci->command_ring);
    if (xhci->scratch_pad) {
        for (size_t i = 0; i < scratch_pad_bufs; i++) {
            if (xhci->scratch_pad[i]) {
                xhci_free(xhci, (void *)xhci_phys_to_virt(xhci, (mx_paddr_t)xhci->scratch_pad[i]));
            }
        }
        xhci_free(xhci, xhci->scratch_pad);
    }
    xhci_free(xhci, xhci->dcbaa);
    free(xhci->slots);
    return result;
}

static void xhci_update_erdp(xhci_t* xhci, int interruptor) {
    xhci_event_ring_t* er = &xhci->event_rings[interruptor];
    xhci_intr_regs_t* intr_regs = &xhci->runtime_regs->intr_regs[interruptor];

    uint64_t erdp = xhci_virt_to_phys(xhci, (mx_vaddr_t)er->current);
    erdp |= ERDP_EHB; // clear event handler busy
    XHCI_WRITE64(&intr_regs->erdp, erdp);
}

static void xhci_interruptor_init(xhci_t* xhci, int interruptor) {
    xhci_intr_regs_t* intr_regs = &xhci->runtime_regs->intr_regs[interruptor];

    xhci_update_erdp(xhci, interruptor);

    XHCI_SET32(&intr_regs->iman, IMAN_IE, IMAN_IE);
    XHCI_SET32(&intr_regs->erstsz, ERSTSZ_MASK, ERST_ARRAY_SIZE);
    XHCI_WRITE64(&intr_regs->erstba, xhci_virt_to_phys(xhci,
                                                       (mx_vaddr_t)xhci->event_rings[interruptor].erst_array));
}

static void xhci_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected) {
    uint32_t value = XHCI_READ32(ptr);
    while ((value & bits) != expected) {
        usleep(1000);
        value = XHCI_READ32(ptr);
    }
}

void xhci_start(xhci_t* xhci) {
    volatile uint32_t* usbcmd = &xhci->op_regs->usbcmd;
    volatile uint32_t* usbsts = &xhci->op_regs->usbsts;

    xhci_wait_bits(usbsts, USBSTS_CNR, 0);

    // stop controller
    XHCI_SET32(usbcmd, USBCMD_RS, 0);
    // wait until USBSTS_HCH signals we stopped
    xhci_wait_bits(usbsts, USBSTS_HCH, USBSTS_HCH);

    XHCI_SET32(usbcmd, USBCMD_HCRST, USBCMD_HCRST);
    xhci_wait_bits(usbcmd, USBCMD_HCRST, 0);
    xhci_wait_bits(usbsts, USBSTS_CNR, 0);

    // setup operational registers
    xhci_op_regs_t* op_regs = xhci->op_regs;
    // initialize command ring
    uint64_t crcr = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->command_ring.start);
    crcr |= CRCR_RCS;
    XHCI_WRITE64(&op_regs->crcr, crcr);

    XHCI_WRITE64(&op_regs->dcbaap, xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->dcbaa));
    XHCI_SET_BITS32(&op_regs->config, CONFIG_MAX_SLOTS_ENABLED_START,
                    CONFIG_MAX_SLOTS_ENABLED_BITS, xhci->max_slots);

    // initialize interruptor (only using one for now)
    xhci_interruptor_init(xhci, 0);

    // start the controller with interrupts enabled
    uint32_t start_flags = USBCMD_RS | USBCMD_INTE;
    XHCI_SET32(usbcmd, start_flags, start_flags);
    xhci_wait_bits(usbsts, USBSTS_HCH, 0);

    xhci_start_device_thread(xhci);
}

void xhci_post_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits,
                       xhci_command_context_t* context) {
    // FIXME - check that command ring is not full?

    mtx_lock(&xhci->command_ring.mutex);

    xhci_transfer_ring_t* cr = &xhci->command_ring;
    xhci_trb_t* trb = cr->current;
    int index = trb - cr->start;
    xhci->command_contexts[index] = context;

    XHCI_WRITE64(&trb->ptr, ptr);
    XHCI_WRITE32(&trb->status, 0);
    trb_set_control(trb, command, control_bits);

    xhci_increment_ring(xhci, cr);

    XHCI_WRITE32(&xhci->doorbells[0], 0);

    mtx_unlock(&xhci->command_ring.mutex);
}

static void xhci_handle_command_complete_event(xhci_t* xhci, xhci_trb_t* event_trb) {
    xhci_trb_t* command_trb = xhci_read_trb_ptr(xhci, event_trb);
    uint32_t cc = XHCI_GET_BITS32(&event_trb->status, EVT_TRB_CC_START, EVT_TRB_CC_BITS);
    xprintf("xhci_handle_command_complete_event slot_id: %d command: %d cc: %d\n",
            (event_trb->control >> TRB_SLOT_ID_START), trb_get_type(command_trb), cc);

    int index = command_trb - xhci->command_ring.start;
    mtx_lock(&xhci->command_ring.mutex);
    xhci_command_context_t* context = xhci->command_contexts[index];
    xhci->command_contexts[index] = NULL;
    mtx_unlock(&xhci->command_ring.mutex);

    context->callback(context->data, cc, command_trb, event_trb);
}

static void xhci_handle_events(xhci_t* xhci, int interruptor) {
    xhci_event_ring_t* er = &xhci->event_rings[interruptor];

    // process all TRBs with cycle bit matching our CCS
    while ((XHCI_READ32(&er->current->control) & TRB_C) == er->ccs) {
        uint32_t type = trb_get_type(er->current);
        switch (type) {
        case TRB_EVENT_COMMAND_COMP:
            xhci_handle_command_complete_event(xhci, er->current);
            break;
        case TRB_EVENT_PORT_STATUS_CHANGE:
            xhci_handle_port_changed_event(xhci, er->current);
            break;
        case TRB_EVENT_TRANSFER:
            xhci_handle_transfer_event(xhci, er->current);
            break;
        default:
            printf("xhci_handle_events: unhandled event type %d\n", type);
            break;
        }

        er->current++;
        if (er->current == er->end) {
            er->current = er->start;
            er->ccs ^= TRB_C;
        }
        xhci_update_erdp(xhci, interruptor);
    }
}

void xhci_handle_interrupt(xhci_t* xhci, bool legacy) {
    volatile uint32_t* usbsts = &xhci->op_regs->usbsts;
    const int interruptor = 0;

    uint32_t status = XHCI_READ32(usbsts);
    uint32_t clear = status & USBSTS_CLEAR_BITS;
    XHCI_WRITE32(usbsts, clear);

    // If we are in legacy IRQ mode, clear the IP (Interrupt Pending) bit
    // from the IMAN register of our interrupter.
    if (legacy) {
        xhci_intr_regs_t* intr_regs = &xhci->runtime_regs->intr_regs[interruptor];
        XHCI_SET32(&intr_regs->iman, IMAN_IP, IMAN_IP);
    }

    if (status & USBSTS_EINT) {
        xhci_handle_events(xhci, interruptor);
    }
}
