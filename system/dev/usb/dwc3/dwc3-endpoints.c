// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <magenta/assert.h>

#include "dwc3.h"
#include "dwc3-regs.h"
#include "dwc3-types.h"

#include <stdio.h>
#include <string.h>

#define EP_FIFO_SIZE    PAGE_SIZE

static mx_paddr_t dwc3_ep_trb_phys(dwc3_endpoint_t* ep, dwc3_trb_t* trb) {
    return io_buffer_phys(&ep->fifo.buffer) + ((void *)trb - (void *)ep->fifo.first);
}

static void dwc3_enable_ep(dwc3_t* dwc, unsigned ep_num, bool enable) {
    volatile void* reg = dwc3_mmio(dwc) + DALEPENA;

    mtx_lock(&dwc->lock);

    uint32_t temp = DWC3_READ32(reg);
    uint32_t bit = 1 << ep_num;

    if (enable) {
        temp |= bit;
    } else {
        temp &= ~bit;
    }
    DWC3_WRITE32(reg, temp);

    mtx_unlock(&dwc->lock);
}

mx_status_t dwc3_ep_fifo_init(dwc3_t* dwc, unsigned ep_num) {
    MX_DEBUG_ASSERT(ep_num < countof(dwc->eps));
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];
    dwc3_fifo_t* fifo = &ep->fifo;

    mx_status_t status = io_buffer_init(&fifo->buffer, EP_FIFO_SIZE, IO_BUFFER_RW);
    if (status != MX_OK) {
        return status;
    }

    fifo->first = io_buffer_virt(&fifo->buffer);
    fifo->next = fifo->first;
    fifo->current = NULL;
    fifo->last = (void *)fifo->first + EP_FIFO_SIZE - sizeof(dwc3_trb_t);

    // set up link TRB pointing back to the start of the fifo
    dwc3_trb_t* trb = fifo->last;
    mx_paddr_t trb_phys = io_buffer_phys(&fifo->buffer);
    trb->ptr_low = (uint32_t)trb_phys;
    trb->ptr_high = (uint32_t)(trb_phys >> 32);
    trb->status = 0;
    trb->control = TRB_TRBCTL_LINK | TRB_HWO;
    io_buffer_cache_op(&ep->fifo.buffer, MX_VMO_OP_CACHE_CLEAN,
                           (trb - ep->fifo.first) * sizeof(*trb), sizeof(*trb));

    return MX_OK;
}

void dwc3_ep_fifo_release(dwc3_t* dwc, unsigned ep_num) {
    MX_DEBUG_ASSERT(ep_num < countof(dwc->eps));
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];

    io_buffer_release(&ep->fifo.buffer);
}

void dwc3_ep_start_transfer(dwc3_t* dwc, unsigned ep_num, unsigned type, mx_paddr_t buffer,
                            size_t length) {
    dprintf(LTRACE, "dwc3_ep_start_transfer ep %u type %u length %zu\n", ep_num, type, length);

    // special case: EP0_OUT and EP0_IN use the same fifo
    dwc3_endpoint_t* ep = (ep_num == EP0_IN ? &dwc->eps[EP0_OUT] : &dwc->eps[ep_num]);

    dwc3_trb_t* trb = ep->fifo.next++;
    if (ep->fifo.next == ep->fifo.last) {
        ep->fifo.next = ep->fifo.first;
    }
    if (ep->fifo.current == NULL) {
        ep->fifo.current = trb;
    }

    trb->ptr_low = (uint32_t)buffer;
    trb->ptr_high = (uint32_t)(buffer >> 32);
    trb->status = TRB_BUFSIZ(length);
    trb->control = type | TRB_LST | TRB_IOC | TRB_HWO;
    io_buffer_cache_op(&ep->fifo.buffer, MX_VMO_OP_CACHE_CLEAN,
                           (trb - ep->fifo.first) * sizeof(*trb), sizeof(*trb));

    dwc3_cmd_ep_start_transfer(dwc, ep_num, dwc3_ep_trb_phys(ep, trb));
}

static void dwc3_ep_queue_next_locked(dwc3_t* dwc, dwc3_endpoint_t* ep) {
    iotxn_t* txn;

    if (ep->current_txn == NULL && ep->got_not_ready &&
        (txn = list_remove_head_type(&ep->queued_txns, iotxn_t, node)) != NULL) {
        ep->current_txn = txn;
        ep->got_not_ready = false;
        if (EP_IN(ep->ep_num)) {
            iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, txn->length);
        }

        // TODO(voydanoff) scatter/gather support
        iotxn_physmap(txn);
        mx_paddr_t phys = iotxn_phys(txn);
        dwc3_ep_start_transfer(dwc, ep->ep_num, TRB_TRBCTL_NORMAL, phys, txn->length);
    }
}

mx_status_t dwc3_ep_config(dwc3_t* dwc, usb_endpoint_descriptor_t* ep_desc,
                                  usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    // convert address to index in range 0 - 31
    // low bit is IN/OUT
    unsigned ep_num = dwc3_ep_num(ep_desc->bEndpointAddress);
    if (ep_num < 2) {
        // index 0 and 1 are for endpoint zero
        return MX_ERR_INVALID_ARGS;
    }

    unsigned ep_type = usb_ep_type(ep_desc);
    if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
        dprintf(ERROR, "dwc3_ep_config: isochronous endpoints are not supported\n");
        return MX_ERR_NOT_SUPPORTED;
    }

    dwc3_endpoint_t* ep = &dwc->eps[ep_num];

    mtx_lock(&ep->lock);
    mx_status_t status = dwc3_ep_fifo_init(dwc, ep_num);
    if (status != MX_OK) {
        dprintf(ERROR, "dwc3_config_ep: dwc3_ep_fifo_init failed %d\n", status);
        mtx_unlock(&ep->lock);
        return status;
    }
    ep->max_packet_size = usb_ep_max_packet(ep_desc);
    ep->type = ep_type;
    ep->interval = ep_desc->bInterval;
    // TODO(voydanoff) USB3 support

    ep->enabled = true;

    if (dwc->configured) {
        dwc3_ep_queue_next_locked(dwc, ep);
    }

    mtx_unlock(&ep->lock);

    return MX_OK;
}

mx_status_t dwc3_ep_disable(dwc3_t* dwc, uint8_t ep_addr) {
    // convert address to index in range 0 - 31
    // low bit is IN/OUT
    unsigned ep_num = dwc3_ep_num(ep_addr);
    if (ep_num < 2) {
        // index 0 and 1 are for endpoint zero
        return MX_ERR_INVALID_ARGS;
    }

    dwc3_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);
    dwc3_ep_fifo_release(dwc, ep_num);
    ep->enabled = false;
    mtx_unlock(&ep->lock);

    return MX_OK;
}

void dwc3_ep_queue(dwc3_t* dwc, unsigned ep_num, iotxn_t* txn) {
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];

    // OUT transactions must have length > 0 and multiple of max packet size
    if (EP_OUT(ep_num)) {
        if (txn->length == 0 || txn->length % ep->max_packet_size != 0) {
            dprintf(ERROR, "dwc3_ep_queue: OUT transfers must be multiple of max packet size\n");
            iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
            return;
        }
    }

    mtx_lock(&ep->lock);

    if (!ep->enabled) {
        mtx_unlock(&ep->lock);
        iotxn_complete(txn, MX_ERR_BAD_STATE, 0);
        return;
    }

    list_add_tail(&ep->queued_txns, &txn->node);

    if (dwc->configured) {
        dwc3_ep_queue_next_locked(dwc, ep);
    }

    mtx_unlock(&ep->lock);
}

void dwc3_ep_set_config(dwc3_t* dwc, unsigned ep_num, bool enable) {
    dprintf(TRACE, "dwc3_ep_set_config %u\n", ep_num);

    dwc3_endpoint_t* ep = &dwc->eps[ep_num];

    if (enable) {
        dwc3_cmd_ep_set_config(dwc, ep_num, ep->type, ep->max_packet_size, ep->interval, false);
        dwc3_cmd_ep_transfer_config(dwc, ep_num);
        dwc3_enable_ep(dwc, ep_num, true);
    } else {
        dwc3_enable_ep(dwc, ep_num, false);
    }
}

void dwc3_start_eps(dwc3_t* dwc) {
    dprintf(TRACE, "dwc3_start_eps\n");

    dwc3_cmd_ep_set_config(dwc, EP0_IN, USB_ENDPOINT_CONTROL, dwc->eps[EP0_IN].max_packet_size, 0,
                           true);
    dwc3_cmd_start_new_config(dwc, EP0_OUT, 2);

    for (unsigned ep_num = 2; ep_num < countof(dwc->eps); ep_num++) {
        dwc3_endpoint_t* ep = &dwc->eps[ep_num];
        if (ep->enabled) {
            dwc3_ep_set_config(dwc, ep_num, true);

            mtx_lock(&ep->lock);
            dwc3_ep_queue_next_locked(dwc, ep);
            mtx_unlock(&ep->lock);
        }
    }
}

static void dwc_ep_read_trb(dwc3_endpoint_t* ep, dwc3_trb_t* trb, dwc3_trb_t* out_trb) {
    if (trb >= ep->fifo.first && trb < ep->fifo.last) {
        io_buffer_cache_op(&ep->fifo.buffer, MX_VMO_OP_CACHE_INVALIDATE,
                           (trb - ep->fifo.first) * sizeof(*trb), sizeof(*trb));
        memcpy((void *)out_trb, (void *)trb, sizeof(*trb));
    } else {
        dprintf(ERROR, "dwc_ep_read_trb: bad trb\n");
    }
}

void dwc3_ep_xfer_started(dwc3_t* dwc, unsigned ep_num, unsigned rsrc_id) {
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);
    ep->rsrc_id = rsrc_id;
    mtx_unlock(&ep->lock);
}

void dwc3_ep_xfer_not_ready(dwc3_t* dwc, unsigned ep_num, unsigned stage) {
    dprintf(LTRACE, "dwc3_ep_xfer_not_ready ep %u state %d\n", ep_num, dwc->ep0_state);

    if (ep_num == EP0_OUT || ep_num == EP0_IN) {
        dwc3_ep0_xfer_not_ready(dwc, ep_num, stage);
    } else {
        dwc3_endpoint_t* ep = &dwc->eps[ep_num];

        mtx_lock(&ep->lock);
        ep->got_not_ready = true;
        dwc3_ep_queue_next_locked(dwc, ep);
        mtx_unlock(&ep->lock);
    }
}

void dwc3_ep_xfer_complete(dwc3_t* dwc, unsigned ep_num) {
    dprintf(LTRACE, "dwc3_ep_xfer_complete ep %u state %d\n", ep_num, dwc->ep0_state);

    if (ep_num >= countof(dwc->eps)) {
        dprintf(ERROR, "dwc3_ep_xfer_complete: bad ep_num %u\n", ep_num);
        return;
    }

    if (ep_num == EP0_OUT || ep_num == EP0_IN) {
        dwc3_ep0_xfer_complete(dwc, ep_num);
    } else {
        dwc3_endpoint_t* ep = &dwc->eps[ep_num];

        mtx_lock(&ep->lock);
        iotxn_t* txn = ep->current_txn;
        ep->current_txn = NULL;

        if (txn) {
            dwc3_trb_t  trb;
            dwc_ep_read_trb(ep, ep->fifo.current, &trb);
            ep->fifo.current = NULL;
            if (trb.control & TRB_HWO) {
                dprintf(ERROR, "TRB_HWO still set in dwc3_ep_xfer_complete\n");
            }

            mx_off_t actual = txn->length - TRB_BUFSIZ(trb.status);
//            dwc3_ep_queue_next_locked(dwc, ep);

            mtx_unlock(&ep->lock);

            if (EP_OUT(ep_num)) {
                iotxn_cacheop(txn, MX_VMO_OP_CACHE_INVALIDATE, 0, actual);
            }
            iotxn_complete(txn, MX_OK, actual);
        } else {
            mtx_unlock(&ep->lock);
            dprintf(ERROR, "dwc3_ep_xfer_complete: no iotxn found to complete!\n");
        }
    }
}

mx_status_t dwc3_ep_set_stall(dwc3_t* dwc, unsigned ep_num, bool stall) {
    if (ep_num >= countof(dwc->eps)) {
        return MX_ERR_INVALID_ARGS;
    }

    dwc3_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);

    if (!ep->enabled) {
        mtx_unlock(&ep->lock);
        return MX_ERR_BAD_STATE;
    }
    if (stall && !ep->stalled) {
        dwc3_cmd_ep_set_stall(dwc, ep_num);
    } else if (!stall && ep->stalled) {
        dwc3_cmd_ep_clear_stall(dwc, ep_num);
    }
    ep->stalled = stall;
    mtx_unlock(&ep->lock);

    return MX_OK;
}

void dwc3_ep_end_transfers(dwc3_t* dwc, unsigned ep_num, mx_status_t reason) {
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);

    if (ep->current_txn) {
        dwc3_cmd_ep_end_transfer(dwc, ep_num);
        iotxn_complete(ep->current_txn, reason, 0);
        ep->current_txn = NULL;
    }

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&ep->queued_txns, iotxn_t, node)) != NULL) {
        iotxn_complete(txn, reason, 0);
    }

    mtx_unlock(&ep->lock);
}
