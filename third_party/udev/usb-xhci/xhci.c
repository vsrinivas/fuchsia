/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2010 Patrick Georgi
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

//#define XHCI_DEBUG
//#define XHCI_SPEW_DEBUG

#include <hw/pci.h>
#include <inttypes.h>
#include <magenta/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xhci-private.h"

#define EPO_BUFFER_SIZE 2048

static void xhci_start(xhci_t* xhci);
static void xhci_stop(xhci_t* xhci);
static void xhci_reset(xhci_t* xhci);
static void xhci_reinit(xhci_t* xhci);
static int xhci_queue_request(mx_device_t* hcidev, int devaddr, usb_request_t* request);
static int xhci_control(mx_device_t* hcidev, int devaddr, usb_setup_t* devreq,
                        int dalen, uint8_t* data);

/*
 * Some structures must not cross page boundaries. To get this,
 * we align them by their size (or the next greater power of 2).
 */
void* xhci_align(xhci_t* xhci, const size_t min_align, const size_t size) {
    size_t align;
    if (!(size & (size - 1)))
        align = size; /* It's a power of 2 */
    else
        align = 1 << ((sizeof(unsigned) << 3) - __builtin_clz(size));
    if (align < min_align)
        align = min_align;
    xhci_spew("Aligning %zu to %zu\n", size, align);
    return xhci_memalign(xhci, align, size);
}

void xhci_clear_trb(trb_t* const trb, const int pcs) {
    trb->ptr_low = 0;
    trb->ptr_high = 0;
    trb->status = 0;
    trb->control = !pcs;
}

void xhci_init_cycle_ring(xhci_t* const xhci, transfer_ring_t* const tr, const size_t ring_size) {
    memset((void*)tr->ring, 0, ring_size * sizeof(*tr->ring));
    TRB_SET(TT, &tr->ring[ring_size - 1], TRB_LINK);
    TRB_SET(TC, &tr->ring[ring_size - 1], 1);
    /* only one segment that points to itself */
    tr->ring[ring_size - 1].ptr_low = xhci_virt_to_phys(xhci, (mx_vaddr_t)tr->ring);

    tr->pcs = 1;
    tr->cur = tr->ring;
}

static long
xhci_handshake(volatile uint32_t* const reg, uint32_t mask, uint32_t wait_for, long timeout_ms) {
    while ((*reg & mask) != wait_for && timeout_ms--)
        usleep(1000);
    return timeout_ms;
}

static int
xhci_wait_ready(xhci_t* const xhci) {
    xhci_debug("Waiting for controller to be ready... ");
    if (!xhci_handshake(&xhci->opreg->usbsts, USBSTS_CNR, 0, 100L)) {
        xhci_debug("timeout!\n");
        return -1;
    }
    xhci_debug("ok.\n");
    return 0;
}

usbdev_t* init_device_entry(usb_xhci_t* hci, int i) {
    xhci_t* const xhci = &hci->xhci;
    usbdev_t* dev = calloc(1, sizeof(usbdev_t));
    if (!dev) {
        xhci_debug("no memory to allocate device structure\n");
        return NULL;
    }
    xhci->devices[i] = dev;
    dev->address = -1;
    dev->hub = -1;
    dev->port = -1;
    dev->hci = hci;
    list_initialize(&dev->req_queue);

    return dev;
}

usb_request_t* xhci_alloc_request(mx_device_t* device, uint16_t length) {
    xhci_t* const xhci = get_xhci(device);
    usb_request_t* request = calloc(1, sizeof(usb_request_t));
    if (!request)
        return NULL;
    // buffers need not be aligned, but 64 byte alignment gives better performance
    request->buffer = (uint8_t*)xhci_memalign(xhci, 64, length);
    if (!request->buffer) {
        printf("could not allocate request buffer\n");
        free(request);
        return NULL;
    }
    request->buffer_length = length;
    return request;
}

void xhci_free_request(mx_device_t* device, usb_request_t* request) {
    xhci_t* const xhci = get_xhci(device);
    if (request) {
        if (request->buffer) {
            xhci_free(xhci, request->buffer);
        }
        free(request);
    }
}

void xhci_set_bus_device(mx_device_t* hcidev, mx_device_t* busdev) {
    xhci_t* const xhci = get_xhci(hcidev);
    if (busdev) {
        xhci->bus_device = busdev;
        device_get_protocol(busdev, MX_PROTOCOL_USB_BUS, (void**)&xhci->bus_protocol);
    } else {
        xhci->bus_device = NULL;
        xhci->bus_protocol = NULL;
    }
}

usb_hci_protocol_t _xhci_protocol = {
    .alloc_request = xhci_alloc_request,
    .free_request = xhci_free_request,
    .queue_request = xhci_queue_request,
    .control = xhci_control,
    .set_address = xhci_set_address,
    .finish_device_config = xhci_finish_device_config,
    .destroy_device = xhci_destroy_dev,
    .set_bus_device = xhci_set_bus_device,
};

void xhci_poll(xhci_t* xhci) {
    list_node_t completed_reqs;

    xhci_rh_check_status_changed(xhci);

    mxr_mutex_lock(&xhci->mutex);
    xhci_handle_events(xhci);
    // move contents of xhci->completed_reqs to a local list within the mutex
    if (list_is_empty(&xhci->completed_reqs)) {
        list_initialize(&completed_reqs);
    } else {
        xhci->completed_reqs.next->prev = &completed_reqs;
        xhci->completed_reqs.prev->next = &completed_reqs;
        completed_reqs.prev = xhci->completed_reqs.prev;
        completed_reqs.next = xhci->completed_reqs.next;

        list_initialize(&xhci->completed_reqs);
    }
    mxr_mutex_unlock(&xhci->mutex);

    usb_request_t* request;
    usb_request_t* prev;
    list_for_every_entry_safe (&completed_reqs, request, prev, usb_request_t, node) {
        list_delete(&request->node);
        request->complete_cb(request);
    }
}

mx_status_t xhci_startup(usb_xhci_t* uxhci) {
    xhci_debug("xhci_pci_startup\n");

    xhci_t* xhci = &uxhci->xhci;
    memset(xhci, 0, sizeof(*xhci));

    xhci->io_alloc = io_alloc_init(1024 * 1024);
    if (!xhci->io_alloc)
        return ERR_NO_MEMORY;
    xhci->ep0_buffer = xhci_malloc(xhci, EPO_BUFFER_SIZE);

    //FIXME roothub parent?
    usbdev_t* rhdev = init_device_entry(uxhci, 0);
    xhci->roothub = rhdev;
    xhci->cr.ring = xhci_align(xhci, 64, COMMAND_RING_SIZE * sizeof(trb_t));
    xhci->er.ring = xhci_align(xhci, 64, EVENT_RING_SIZE * sizeof(trb_t));
    xhci->ev_ring_table = xhci_align(xhci, 64, sizeof(erst_entry_t));
    if (!xhci->roothub || !xhci->cr.ring ||
        !xhci->er.ring || !xhci->ev_ring_table) {
        xhci_debug("Out of memory\n");
        goto _free_xhci;
    }

    xhci->capreg = uxhci->mmio;
    xhci->opreg = ((void*)xhci->capreg) + xhci->capreg->caplength;
    xhci->hcrreg = ((void*)xhci->capreg) + xhci->capreg->rtsoff;
    xhci->dbreg = ((void*)xhci->capreg) + xhci->capreg->dboff;
    xhci_debug("caplen:  0x%" PRIx32 "\n", xhci->capreg->caplength);
    xhci_debug("rtsoff:  0x%" PRIx32 "\n", xhci->capreg->rtsoff);
    xhci_debug("dboff:   0x%" PRIx32 "\n", xhci->capreg->dboff);

    xhci_debug("hciversion: %0x.%02x\n",
               xhci->capreg->hciver_hi, xhci->capreg->hciver_lo);
    if ((xhci->capreg->hciversion < 0x96) ||
        (xhci->capreg->hciversion > 0x100)) {
        xhci_debug("Unsupported xHCI version\n");
        goto _free_xhci;
    }

    xhci_debug("context size: %dB\n", CTXSIZE(xhci));
    xhci_debug("maxslots: 0x%02x\n", xhci->capreg->MaxSlots);
    xhci_debug("maxports: 0x%02x\n", xhci->capreg->MaxPorts);
    const unsigned pagesize = xhci->opreg->pagesize << 12;
    xhci_debug("pagesize: 0x%04x\n", pagesize);

    /*
	 * We haven't touched the hardware yet. So we allocate all dynamic
	 * structures at first and can still chicken out easily if we run out
	 * of memory.
	 */
    xhci->max_slots_en = xhci->capreg->MaxSlots & CONFIG_LP_MASK_MaxSlotsEn;
    xhci->dcbaa = xhci_align(xhci, 64, (xhci->max_slots_en + 1) * sizeof(uint64_t));
    xhci->dev = malloc((xhci->max_slots_en + 1) * sizeof(*xhci->dev));
    if (!xhci->dcbaa || !xhci->dev) {
        xhci_debug("Out of memory\n");
        goto _free_xhci;
    }
    memset(xhci->dcbaa, 0x00, (xhci->max_slots_en + 1) * sizeof(uint64_t));
    memset(xhci->dev, 0x00, (xhci->max_slots_en + 1) * sizeof(*xhci->dev));

    /*
	 * Let dcbaa[0] point to another array of pointers, sp_ptrs.
	 * The pointers therein point to scratchpad buffers (pages).
	 */
    const size_t max_sp_bufs = xhci->capreg->Max_Scratchpad_Bufs_Hi << 5 |
                               xhci->capreg->Max_Scratchpad_Bufs_Lo;
    xhci_debug("max scratchpad bufs: 0x%zx\n", max_sp_bufs);
    if (max_sp_bufs) {
        const size_t sp_ptrs_size = max_sp_bufs * sizeof(uint64_t);
        xhci->sp_ptrs = xhci_align(xhci, 64, sp_ptrs_size);
        if (!xhci->sp_ptrs) {
            xhci_debug("Out of memory\n");
            goto _free_xhci_structs;
        }
        memset(xhci->sp_ptrs, 0x00, sp_ptrs_size);
        for (size_t i = 0; i < max_sp_bufs; ++i) {
            /* Could use mmap() here if we had it.
			   Maybe there is another way. */
            void* const page = xhci_memalign(xhci, pagesize, pagesize);
            if (!page) {
                xhci_debug("Out of memory\n");
                goto _free_xhci_structs;
            }
            xhci->sp_ptrs[i] = xhci_virt_to_phys(xhci, (mx_vaddr_t)page);
        }
        xhci->dcbaa[0] = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->sp_ptrs);
    }

    /* Now start working on the hardware */
    if (xhci_wait_ready(xhci)) {
        goto _free_xhci_structs;
    }

    /* TODO: Check if BIOS claims ownership (and hand over) */

    xhci_reset(xhci);
    xhci_reinit(xhci);

    xhci_rh_init(uxhci);
    list_initialize(&xhci->completed_reqs);

    return NO_ERROR;

_free_xhci_structs:
    if (xhci->sp_ptrs) {
        for (size_t i = 0; i < max_sp_bufs; ++i) {
            if (xhci->sp_ptrs[i])
                xhci_free_phys(xhci, xhci->sp_ptrs[i]);
        }
    }
    free(xhci->sp_ptrs);
    free(xhci->dcbaa);
_free_xhci:
    xhci_free(xhci, (void*)xhci->ev_ring_table);
    xhci_free(xhci, (void*)xhci->er.ring);
    xhci_free(xhci, (void*)xhci->cr.ring);
    free(xhci->roothub);
    free(xhci->dev);
    /* _free_controller: */
    xhci_destroy_dev(&uxhci->hcidev, 0);

    return -1;
}

static void
xhci_reset(xhci_t* const xhci) {
    xhci_stop(xhci);

    xhci->opreg->usbcmd |= USBCMD_HCRST;

/* Existing Intel xHCI controllers require a delay of 1 ms,
	 * after setting the CMD_RESET bit, and before accessing any
	 * HC registers. This allows the HC to complete the
	 * reset operation and be ready for HC register access.
	 * Without this delay, the subsequent HC register access,
	 * may result in a system hang very rarely.
	 */
#if ARCH_X86_32 || ARCH_X86_64
    usleep(1000);
#endif

    xhci_debug("Resetting controller... ");
    if (!xhci_handshake(&xhci->opreg->usbcmd, USBCMD_HCRST, 0, 1000L))
        xhci_debug("timeout!\n");
    else
        xhci_debug("ok.\n");
}

static void
xhci_reinit(xhci_t* xhci) {
    if (xhci_wait_ready(xhci))
        return;

    /* Enable all available slots */
    xhci->opreg->config = xhci->max_slots_en;

    /* Set DCBAA */
    xhci->opreg->dcbaap_lo = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->dcbaa);
    xhci->opreg->dcbaap_hi = 0;

    /* Initialize command ring */
    xhci_init_cycle_ring(xhci, &xhci->cr, COMMAND_RING_SIZE);
    xhci_debug("command ring @%p (%p)\n",
               xhci->cr.ring, (void*)xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->cr.ring));
    xhci->opreg->crcr_lo = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->cr.ring) | CRCR_RCS;
    xhci->opreg->crcr_hi = 0;

    /* Make sure interrupts are enabled */
    xhci->opreg->usbcmd |= USBCMD_INTE;

    /* Initialize event ring */
    xhci_reset_event_ring(&xhci->er);
    xhci_debug("event ring @%p (%p)\n",
               xhci->er.ring, (void*)xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->er.ring));
    xhci_debug("ERST Max: 0x%x ->  0x%x entries\n",
               xhci->capreg->ERST_Max, 1 << xhci->capreg->ERST_Max);
    memset((void*)xhci->ev_ring_table, 0x00, sizeof(erst_entry_t));
    xhci->ev_ring_table[0].seg_base_lo = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->er.ring);
    xhci->ev_ring_table[0].seg_base_hi = 0;
    xhci->ev_ring_table[0].seg_size = EVENT_RING_SIZE;

    /* pass event ring table to hardware */
    wmb();
    /* Initialize primary interrupter */
    xhci->hcrreg->intrrs[0].erstsz = 1;
    xhci_update_event_dq(xhci);
    /* erstba has to be written at last */
    xhci->hcrreg->intrrs[0].erstba_lo = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->ev_ring_table);
    xhci->hcrreg->intrrs[0].erstba_hi = 0;

    /* enable interrupts */
    xhci->hcrreg->intrrs[0].iman |= IMAN_IE;

    xhci_start(xhci);

#ifdef USB_DEBUG
    int i;
    for (i = 0; i < 32; ++i) {
        xhci_debug("NOOP run #%d\n", i);
        trb_t* const cmd = xhci_next_command_trb(xhci);
        TRB_SET(TT, cmd, TRB_CMD_NOOP);

        xhci_post_command(xhci);

        /* Wait for result in event ring */
        xhci_wait_for_command_done(xhci, cmd, 1);
        xhci_debug("Command ring is %srunning\n",
                   (xhci->opreg->crcr_lo & CRCR_CRR) ? "" : "not ");
    }
#endif
}

/* FIXME
static void
xhci_shutdown(hci_t *const controller)
{
	int i;

	if (controller == 0)
		return;

	detach_controller(controller);

	xhci_t *const xhci = XHCI_INST(controller);
	xhci_stop(controller);

	if (xhci->sp_ptrs) {
		size_t max_sp_bufs = xhci->capreg->Max_Scratchpad_Bufs_Hi << 5 |
				     xhci->capreg->Max_Scratchpad_Bufs_Lo;
		for (i = 0; i < max_sp_bufs; ++i) {
			if (xhci->sp_ptrs[i])
				xhci_free_phys(xhci, xhci->sp_ptrs[i]);
		}
	}
	xhci_free(xhci, xhci->sp_ptrs);
	xhci_free(xhci, xhci->dcbaa);
	free(xhci->dev);
	xhci_free(xhci, (void *)xhci->ev_ring_table);
	xhci_free(xhci, (void *)xhci->er.ring);
	xhci_free(xhci, (void *)xhci->cr.ring);
	xhci_free(xhci, (void *)xhci->ep0_buffer);
    io_alloc_free(xhci->io_alloc);
	free(xhci);
	free(controller);
}
*/

static void
xhci_start(xhci_t* xhci) {
    xhci->opreg->usbcmd |= USBCMD_RS;
    if (!xhci_handshake(&xhci->opreg->usbsts, USBSTS_HCH, 0, 1000L))
        xhci_debug("Controller didn't start within 1s\n");
}

static void
xhci_stop(xhci_t* xhci) {
    xhci->opreg->usbcmd &= ~USBCMD_RS;
    if (!xhci_handshake(&xhci->opreg->usbsts,
                        USBSTS_HCH, USBSTS_HCH, 1000L))
        xhci_debug("Controller didn't halt within 1s\n");
}

static int
xhci_reset_endpoint(xhci_t* xhci, int slot_id, usb_endpoint_t* const ep) {
    const int ep_id = ep ? xhci_ep_id(ep) : 1;
    epctx_t* const epctx = xhci->dev[slot_id].ctx.ep[ep_id];

    xhci_debug("Resetting ID %d EP %d (ep state: %d)\n",
               slot_id, ep_id, EC_GET(STATE, epctx));

    /* Run Reset Endpoint Command if the EP is in Halted state */
    if (EC_GET(STATE, epctx) == 2) {
        const int cc = xhci_cmd_reset_endpoint(xhci, slot_id, ep_id);
        if (cc != CC_SUCCESS) {
            xhci_debug("Reset Endpoint Command failed: %d\n", cc);
            return 1;
        }
    }

    /* Clear TT buffer for bulk and control endpoints behind a TT */
    usbdev_t* dev = xhci->devices[slot_id];
    const int hub = dev->hub;
    if (hub && dev->speed < USB_SPEED_HIGH &&
        xhci->devices[hub]->speed == USB_SPEED_HIGH) {
        /* TODO */;
    }

    /* Reset transfer ring if the endpoint is in the right state */
    const unsigned ep_state = EC_GET(STATE, epctx);
    if (ep_state == 3 || ep_state == 4) {
        transfer_ring_t* const tr =
            xhci->dev[slot_id].transfer_rings[ep_id];
        const int cc = xhci_cmd_set_tr_dq(xhci, slot_id, ep_id,
                                          tr->ring, 1);
        if (cc != CC_SUCCESS) {
            xhci_debug("Set TR Dequeue Command failed: %d\n", cc);
            return 1;
        }
        xhci_init_cycle_ring(xhci, tr, TRANSFER_RING_SIZE);
    }

    xhci_debug("Finished resetting ID %d EP %d (ep state: %d)\n",
               slot_id, ep_id, EC_GET(STATE, epctx));

    return 0;
}

static void
xhci_enqueue_trb(xhci_t* const xhci, transfer_ring_t* const tr) {
    const int chain = TRB_GET(CH, tr->cur);
    TRB_SET(C, tr->cur, tr->pcs);
    ++tr->cur;

    while (TRB_GET(TT, tr->cur) == TRB_LINK) {
        xhci_spew("Handling LINK pointer\n");
        const int tc = TRB_GET(TC, tr->cur);
        TRB_SET(CH, tr->cur, chain);
        wmb();
        TRB_SET(C, tr->cur, tr->pcs);
        tr->cur = (trb_t*)xhci_phys_to_virt(xhci, tr->cur->ptr_low);
        if (tc)
            tr->pcs ^= 1;
    }
}

static trb_t*
xhci_enqueue_td(xhci_t* const xhci, transfer_ring_t* const tr, const int ep, const size_t mps,
                const int dalen, void* const data, const int dir) {
    trb_t* trb = NULL;                         /* cur TRB */
    uint8_t* cur_start = data;                 /* cur data pointer */
    size_t length = dalen;                     /* remaining bytes */
    size_t packets = (length + mps - 1) / mps; /* remaining packets */
    size_t residue = 0;                        /* residue from last TRB */
    size_t trb_count = 0;                      /* TRBs added so far */

    while (length || !trb_count /* enqueue at least one */) {
        const size_t cur_end = ((size_t)cur_start + 0x10000) & ~0xffff;
        size_t cur_length = cur_end - (size_t)cur_start;
        if (length < cur_length) {
            cur_length = length;
            packets = 0;
            length = 0;
        } else if (1 /* !IS_ENABLED(CONFIG_LP_USB_XHCI_MTK_QUIRK) */) {
            packets -= (residue + cur_length) / mps;
            residue = (residue + cur_length) % mps;
            length -= cur_length;
        }

        trb = tr->cur;
        xhci_clear_trb(trb, tr->pcs);
        trb->ptr_low = (uint32_t)xhci_virt_to_phys(xhci, (mx_vaddr_t)cur_start);
        TRB_SET(TL, trb, cur_length);
        TRB_SET(TDS, trb, MIN(TRB_MAX_TD_SIZE, packets));
        TRB_SET(CH, trb, 1);

        if (length && 0 /* IS_ENABLED(CONFIG_LP_USB_XHCI_MTK_QUIRK) */) {
            /*
			 * For MTK's xHCI controller, TDS defines a number of
			 * packets that remain to be transferred for a TD after
			 * processing all Max packets in all previous TRBs, that
			 * means don't include the current TRB's.
			 */
            packets -= (residue + cur_length) / mps;
            residue = (residue + cur_length) % mps;
            length -= cur_length;
        }

        /* Check for first, data stage TRB */
        if (!trb_count && ep == 1) {
            TRB_SET(DIR, trb, dir);
            TRB_SET(TT, trb, TRB_DATA_STAGE);
        } else {
            TRB_SET(TT, trb, TRB_NORMAL);
        }
        /*
		 * This is a workaround for Synopsys DWC3. If the ENT flag is
		 * not set for the Normal and Data Stage TRBs. We get Event TRB
		 * with length 0x20d from the controller when we enqueue a TRB
		 * for the IN endpoint with length 0x200.
		 */
        if (!length)
            TRB_SET(ENT, trb, 1);

        xhci_enqueue_trb(xhci, tr);

        cur_start += cur_length;
        ++trb_count;
    }

    trb = tr->cur;
    xhci_clear_trb(trb, tr->pcs);
    trb->ptr_low = (uint32_t)xhci_virt_to_phys(xhci, (mx_vaddr_t)trb); /* for easier debugging only */
    TRB_SET(TT, trb, TRB_EVENT_DATA);
    TRB_SET(IOC, trb, 1);

    xhci_enqueue_trb(xhci, tr);
    return trb;
}

static int
xhci_control(mx_device_t* hcidev, int devaddr, usb_setup_t* const devreq,
             const int dalen, unsigned char* const src) {
    xhci_spew("xhci_control %02X %02X %04X %04X %04X\n", devreq->bmRequestType, devreq->bRequest,
              devreq->wValue, devreq->wIndex, devreq->wLength);
    unsigned char* data = src;
    xhci_t* const xhci = get_xhci(hcidev);
    epctx_t* const epctx = xhci->dev[devaddr].ctx.ep0;
    transfer_ring_t* const tr = xhci->dev[devaddr].transfer_rings[1];
    bool out = ((devreq->bmRequestType & USB_DIR_MASK) == USB_DIR_OUT);

    const size_t off = (size_t)data & 0xffff;
    if ((off + dalen) > ((TRANSFER_RING_SIZE - 4) << 16)) {
        xhci_debug("Unsupported transfer size\n");
        return -1;
    }

    mxr_mutex_lock(&xhci->mutex);

    if (dalen > 0) {
        data = xhci->ep0_buffer;
        if (dalen > EPO_BUFFER_SIZE) {
            xhci_debug("Control transfer too large: %d\n", dalen);
            mxr_mutex_unlock(&xhci->mutex);
            return -1;
        }
        if (out)
            memcpy(data, src, dalen);
    }

    /* Reset endpoint if it's not running */
    const unsigned ep_state = EC_GET(STATE, epctx);
    if (ep_state > 1) {
        if (xhci_reset_endpoint(xhci, devaddr, NULL)) {
            mxr_mutex_unlock(&xhci->mutex);
            return -1;
        }
    }

    /* Fill and enqueue setup TRB */
    trb_t* const setup = tr->cur;
    xhci_clear_trb(setup, tr->pcs);
    setup->ptr_low = ((uint32_t*)devreq)[0];
    setup->ptr_high = ((uint32_t*)devreq)[1];
    TRB_SET(TL, setup, 8);
    TRB_SET(TRT, setup, (dalen)
                            ? (out ? TRB_TRT_OUT_DATA : TRB_TRT_IN_DATA)
                            : TRB_TRT_NO_DATA);
    TRB_SET(TT, setup, TRB_SETUP_STAGE);
    TRB_SET(IDT, setup, 1);
    TRB_SET(IOC, setup, 1);
    xhci_enqueue_trb(xhci, tr);

    /* Fill and enqueue data TRBs (if any) */
    if (dalen) {
        const unsigned mps = EC_GET(MPS, epctx);
        const unsigned dt_dir = out ? TRB_DIR_OUT : TRB_DIR_IN;
        xhci_enqueue_td(xhci, tr, 1, mps, dalen, data, dt_dir);
    }

    /* Fill status TRB */
    trb_t* const status = tr->cur;
    xhci_clear_trb(status, tr->pcs);
    TRB_SET(DIR, status, out ? TRB_DIR_IN : TRB_DIR_OUT);
    TRB_SET(TT, status, TRB_STATUS_STAGE);
    TRB_SET(IOC, status, 1);
    xhci_enqueue_trb(xhci, tr);

    /* Ring doorbell for EP0 */
    xhci->dbreg[devaddr] = 1;

    /* Wait for transfer events */
    int i, transferred = 0;
    const int n_stages = 2 + !!dalen;
    for (i = 0; i < n_stages; ++i) {
        const int ret = xhci_wait_for_transfer(xhci, devaddr, 1);
        transferred += ret;
        if (ret < 0) {
            if (ret == TIMEOUT) {
                xhci_debug("Stopping ID %d EP 1\n",
                           devaddr);
                xhci_cmd_stop_endpoint(xhci, devaddr, 1);
            }
            xhci_debug(
                "Stage %d/%d failed: %d\n"
                "  trb ring:   @%p\n"
                "  setup trb:  @%p\n"
                "  status trb: @%p\n"
                "  ep state:   %d -> %d\n"
                "  usbsts:     0x%08" PRIx32 "\n",
                i, n_stages, ret,
                tr->ring, setup, status,
                ep_state, EC_GET(STATE, epctx),
                xhci->opreg->usbsts);
            mxr_mutex_unlock(&xhci->mutex);
            return ret;
        }
    }

    if (!out && data != src)
        memcpy(src, data, transferred);
    mxr_mutex_unlock(&xhci->mutex);
    return transferred;
}

int xhci_get_descriptor(usbdev_t* dev, int rtype, int desc_type, int desc_idx,
                        void* data, size_t len) {
    usb_setup_t dr;

    dr.bmRequestType = rtype;
    dr.bRequest = USB_REQ_GET_DESCRIPTOR;
    dr.wValue = desc_type << 8 | desc_idx;
    dr.wIndex = 0;
    dr.wLength = len;

    return xhci_control(&dev->hci->hcidev, dev->address, &dr, len, data);
}

static int
xhci_queue_request(mx_device_t* hcidev, int slot_id, usb_request_t* request) {
    if (request->endpoint->type != USB_ENDPOINT_BULK && request->endpoint->type != USB_ENDPOINT_INTERRUPT) {
        return ERR_NOT_SUPPORTED;
    }

    xhci_t* xhci = get_xhci(hcidev);
    uint8_t* data = request->buffer;
    size_t size = request->transfer_length;
    usb_endpoint_t* ep = request->endpoint;

    const int ep_id = xhci_ep_id(ep);
    epctx_t* const epctx = xhci->dev[slot_id].ctx.ep[ep_id];
    transfer_ring_t* const tr = xhci->dev[slot_id].transfer_rings[ep_id];

    const size_t off = (size_t)data & 0xffff;
    if ((off + request->transfer_length) > ((TRANSFER_RING_SIZE - 2) << 16)) {
        xhci_debug("Unsupported transfer size\n");
        return ERR_TOO_BIG;
    }

    mxr_mutex_lock(&xhci->mutex);

    /* Reset endpoint if it's not running */
    const unsigned ep_state = EC_GET(STATE, epctx);
    if (ep_state > 1) {
        if (xhci_reset_endpoint(xhci, slot_id, ep)) {
            mxr_mutex_unlock(&xhci->mutex);
            return ERR_BAD_STATE;
        }
    }

    /* Enqueue transfer and ring doorbell */
    const unsigned mps = EC_GET(MPS, epctx);
    const unsigned dir = (ep->direction == USB_ENDPOINT_OUT) ? TRB_DIR_OUT : TRB_DIR_IN;
    request->driver_data = (void*)xhci_enqueue_td(xhci, tr, ep_id, mps, size, data, dir);
    xhci->dbreg[slot_id] = ep_id;

    list_add_tail(&xhci->devices[slot_id]->req_queue, &request->node);

    mxr_mutex_unlock(&xhci->mutex);
    return NO_ERROR;
}

static trb_t*
xhci_next_trb(xhci_t* const xhci, trb_t* cur, int* const pcs) {
    ++cur;
    while (TRB_GET(TT, cur) == TRB_LINK) {
        if (pcs && TRB_GET(TC, cur))
            *pcs ^= 1;
        cur = (trb_t*)xhci_phys_to_virt(xhci, cur->ptr_low);
    }
    return cur;
}

mx_paddr_t xhci_virt_to_phys(xhci_t* const xhci, mx_vaddr_t addr) {
    return io_virt_to_phys(xhci->io_alloc, addr);
}

mx_vaddr_t xhci_phys_to_virt(xhci_t* const xhci, mx_paddr_t addr) {
    return io_phys_to_virt(xhci->io_alloc, addr);
}

void* xhci_malloc(xhci_t* const xhci, size_t size) {
    return io_malloc(xhci->io_alloc, size);
}

void* xhci_memalign(xhci_t* const xhci, size_t alignment, size_t size) {
    return io_memalign(xhci->io_alloc, alignment, size);
}

void xhci_free(xhci_t* const xhci, void* addr) {
    io_free(xhci->io_alloc, addr);
}

void xhci_free_phys(xhci_t* const xhci, mx_paddr_t addr) {
    io_free(xhci->io_alloc, (void*)io_phys_to_virt(xhci->io_alloc, addr));
}
