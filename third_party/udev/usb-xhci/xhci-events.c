/*
 * This file is part of the libpayload project.
 *
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

//#define XHCI_SPEW_DEBUG

#include <assert.h>
#include <unistd.h>
#include "xhci-private.h"

void xhci_reset_event_ring(event_ring_t* const er) {
    int i;
    for (i = 0; i < EVENT_RING_SIZE; ++i)
        er->ring[i].control &= ~TRB_CYCLE;
    er->cur = er->ring;
    er->last = er->ring + EVENT_RING_SIZE;
    er->ccs = 1;
    er->adv = 1;
}

static inline int
xhci_event_ready(const event_ring_t* const er) {
    return (er->cur->control & TRB_CYCLE) == er->ccs;
}

void xhci_update_event_dq(xhci_t* const xhci) {
    if (xhci->er.adv) {
        xhci_spew("Updating dq ptr: @%p(0x%08" PRIx32 ") -> %p\n",
                  xhci_phys_to_virt(xhci, xhci->hcrreg->intrrs[0].erdp_lo),
                  xhci->hcrreg->intrrs[0].erdp_lo, xhci->er.cur);

        uint64_t next_erdp = xhci_virt_to_phys(xhci, (mx_vaddr_t)xhci->er.cur);
        assert(!(next_erdp & ~ERDP_ADDR_MASK));

        // Clear the EHB (Event Handler Busy) bit by writing a 1 to it.
        next_erdp |= ERDP_EHB;

        // TODO(johngro) : Or in the DESI (Dequeue ERST Segment Index) based on
        // the segment which contains the new Dequeue pointer value we are
        // updating the ERDP with.  Right now, this system only supports a
        // single segment, so the DESI value will always be 0.
        //
        // See section 5.5.2.3.3 of the XHCI spec, rev 1.1
        //
        // next_erdp |= (xhci->er.cur_segment & ERDP_DESI_MASK);
        
        xhci->hcrreg->intrrs[0].erdp_lo = (uint32_t)(next_erdp & 0xFFFFFFFF);
        xhci->hcrreg->intrrs[0].erdp_hi = (uint32_t)(next_erdp >> 32);
        xhci->er.adv = 0;
    }
}

void xhci_advance_event_ring(xhci_t* const xhci) {
    xhci->er.cur++;
    xhci->er.adv = 1;
    if (xhci->er.cur == xhci->er.last) {
        xhci_spew("Roll over in event ring\n");
        xhci->er.cur = xhci->er.ring;
        xhci->er.ccs ^= 1;
        xhci_update_event_dq(xhci);
    }
}

// must hold mutex when calling this
static void
xhci_handle_transfer_event(xhci_t* const xhci) {
    const trb_t* const ev = xhci->er.cur;
    const int cc = TRB_GET(CC, ev);
    const int id = TRB_GET(ID, ev);

    if (id && id <= xhci->max_slots_en) {
        trb_t* driver_trb = (trb_t*)xhci_phys_to_virt(xhci, (mx_paddr_t)ev->ptr_low);
        usb_request_t* request;
        usb_request_t* temp;
        list_for_every_entry_safe (&xhci->devices[id]->req_queue, request, temp, usb_request_t, node) {
            if (request->driver_data == driver_trb) {
                if (cc == CC_SUCCESS || cc == CC_SHORT_PACKET) {
                    request->status = NO_ERROR;
                    request->transfer_length = TRB_GET(EVTL, ev);
                } else {
                    request->status = -1;
                    request->transfer_length = 0;
                }
                list_delete(&request->node);
                list_add_tail(&xhci->completed_reqs, &request->node);
                break;
            }
        }
    } else if (cc == CC_STOPPED || cc == CC_STOPPED_LENGTH_INVALID) {
        /* Ignore 'Forced Stop Events' */
    } else {
        xhci_debug(
            "Warning: "
            "Spurious transfer event for ID %d, EP %d:\n"
            "  Pointer: 0x%08x%08x\n"
            "       TL: 0x%06x\n"
            "       CC: %d\n",
            id, ep,
            ev->ptr_high, ev->ptr_low,
            TRB_GET(EVTL, ev), cc);
    }
    xhci_advance_event_ring(xhci);
}

static void
xhci_handle_command_completion_event(xhci_t* const xhci) {
#ifdef XHCI_DEBUG
    const trb_t* const ev = xhci->er.cur;
#endif

    xhci_debug(
        "Warning: Spurious command completion event:\n"
        "  Pointer: 0x%08x%08x\n"
        "       CC: %d\n"
        "  Slot ID: %d\n"
        "    Cycle: %d\n",
        ev->ptr_high, ev->ptr_low,
        TRB_GET(CC, ev), TRB_GET(ID, ev), ev->control & TRB_CYCLE);
    xhci_advance_event_ring(xhci);
}

static void
xhci_handle_host_controller_event(xhci_t* const xhci) {
    const trb_t* const ev = xhci->er.cur;

    const int cc = TRB_GET(CC, ev);
    switch (cc) {
    case CC_EVENT_RING_FULL_ERROR:
        xhci_debug("Event ring full! (@%p)\n", xhci->er.cur);
        /*
		 * If we get here, we have processed the whole queue:
		 * xHC pushes this event, when it sees the ring full,
		 * full of other events.
		 * IMO it's save and necessary to update the dequeue
		 * pointer here.
		 */
        xhci_advance_event_ring(xhci);
        xhci_update_event_dq(xhci);
        break;
    default:
        xhci_debug("Warning: Spurious host controller event: %d\n", cc);
        xhci_advance_event_ring(xhci);
        break;
    }
}

/* handle standard types:
 * - command completion event
 * - port status change event
 * - transfer event
 * - host controller event
 *  must hold mutex when calling this
 */
static void
xhci_handle_event(xhci_t* const xhci) {
    const trb_t* const ev = xhci->er.cur;

    const int trb_type = TRB_GET(TT, ev);
    switch (trb_type) {
    /* Either pass along the event or advance event ring */
    case TRB_EV_TRANSFER:
        xhci_handle_transfer_event(xhci);
        break;
    case TRB_EV_CMD_CMPL:
        xhci_handle_command_completion_event(xhci);
        break;
    case TRB_EV_PORTSC:
        xhci_debug("Port Status Change Event for %d: %d\n",
                   TRB_GET(PORT, ev), TRB_GET(CC, ev));
        /* We ignore the event as we look for the PORTSC
		   registers instead, at a time when it suits _us_. */
        xhci_advance_event_ring(xhci);
        break;
    case TRB_EV_HOST:
        xhci_handle_host_controller_event(xhci);
        break;
    default:
        xhci_debug("Warning: Spurious event: %d, Completion Code: %d\n",
                   trb_type, TRB_GET(CC, ev));
        xhci_advance_event_ring(xhci);
        break;
    }
}

// must hold mutex when calling this
void xhci_handle_events(xhci_t* const xhci) {
    while (xhci_event_ready(&xhci->er))
        xhci_handle_event(xhci);
    xhci_update_event_dq(xhci);
}

static unsigned long
xhci_wait_for_event(const event_ring_t* const er,
                    unsigned long* const timeout_ms) {
    while (!xhci_event_ready(er) && *timeout_ms) {
        --*timeout_ms;
        usleep(1000);
    }
    return *timeout_ms;
}

static unsigned long
xhci_wait_for_event_type(xhci_t* const xhci,
                         uint32_t trb_type,
                         unsigned long* const timeout_ms) {
    while (xhci_wait_for_event(&xhci->er, timeout_ms)) {
        if (TRB_GET(TT, xhci->er.cur) == trb_type)
            break;

        xhci_handle_event(xhci);
    }
    return *timeout_ms;
}

/* returns cc of command in question (pointed to by `address`) */
int xhci_wait_for_command_aborted(xhci_t* const xhci, const trb_t* const address) {
    /*
	 * Specification says that something might be seriously wrong, if
	 * we don't get a response after 5s. Still, let the caller decide,
	 * what to do then.
	 */
    unsigned long timeout_ms = 5 * 1000; /* 5s */
    int cc = TIMEOUT;
    /*
	 * Expects two command completion events:
	 * The first with CC == COMMAND_ABORTED should point to address,
	 * the second with CC == COMMAND_RING_STOPPED should point to new dq.
	 */
    while (xhci_wait_for_event_type(xhci, TRB_EV_CMD_CMPL, &timeout_ms)) {
        if ((xhci->er.cur->ptr_low == (uint32_t)xhci_virt_to_phys(xhci, (mx_vaddr_t)address)) &&
            (xhci->er.cur->ptr_high == 0)) {
            cc = (int)TRB_GET(CC, xhci->er.cur);
            xhci_advance_event_ring(xhci);
            break;
        }

        xhci_handle_command_completion_event(xhci);
    }
    if (!timeout_ms)
        xhci_debug("Warning: Timed out waiting for COMMAND_ABORTED.\n");
    while (xhci_wait_for_event_type(xhci, TRB_EV_CMD_CMPL, &timeout_ms)) {
        if (TRB_GET(CC, xhci->er.cur) == CC_COMMAND_RING_STOPPED) {
            xhci->cr.cur = (trb_t*)xhci_phys_to_virt(xhci, xhci->er.cur->ptr_low);
            xhci_advance_event_ring(xhci);
            break;
        }

        xhci_handle_command_completion_event(xhci);
    }
    if (!timeout_ms)
        xhci_debug(
            "Warning: Timed out "
            "waiting for COMMAND_RING_STOPPED.\n");
    xhci_update_event_dq(xhci);
    return cc;
}

/*
 * returns cc of command in question (pointed to by `address`)
 * caller should abort command if cc is TIMEOUT
 */
int xhci_wait_for_command_done(xhci_t* const xhci,
                               const trb_t* const address,
                               const int clear_event) {
    /*
	 * The Address Device Command should take most time, as it has to
	 * communicate with the USB device. Set address processing shouldn't
	 * take longer than 50ms (at the slave). Let's take a timeout of
	 * 100ms.
	 */
    unsigned long timeout_ms = 100; /* 100ms */
    int cc = TIMEOUT;
    while (xhci_wait_for_event_type(xhci, TRB_EV_CMD_CMPL, &timeout_ms)) {
        if ((xhci->er.cur->ptr_low == xhci_virt_to_phys(xhci, (mx_vaddr_t)address)) &&
            (xhci->er.cur->ptr_high == 0)) {
            cc = TRB_GET(CC, xhci->er.cur);
            break;
        }

        xhci_handle_command_completion_event(xhci);
    }
    if (!timeout_ms) {
        xhci_debug("Warning: Timed out waiting for TRB_EV_CMD_CMPL.\n");
    } else if (clear_event) {
        xhci_advance_event_ring(xhci);
    }
    xhci_update_event_dq(xhci);
    return cc;
}

/* returns amount of bytes transferred on success, negative CC on error */
int xhci_wait_for_transfer(xhci_t* const xhci, uint32_t slot_id, uint32_t ep_id) {
    xhci_spew("Waiting for transfer on ID %d EP %d\n", slot_id, ep_id);
    /* 3s for all types of transfers */ /* TODO: test, wait longer? */
    unsigned long timeout_ms = 3 * 1000;
    int ret = TIMEOUT;
    while (xhci_wait_for_event_type(xhci, TRB_EV_TRANSFER, &timeout_ms)) {
        if (TRB_GET(ID, xhci->er.cur) == slot_id &&
            TRB_GET(EP, xhci->er.cur) == ep_id) {
            ret = -TRB_GET(CC, xhci->er.cur);
            if (ret == -CC_SUCCESS || ret == -CC_SHORT_PACKET)
                ret = TRB_GET(EVTL, xhci->er.cur);
            xhci_advance_event_ring(xhci);
            break;
        }

        xhci_handle_transfer_event(xhci);
    }
    if (!timeout_ms)
        xhci_debug("Warning: Timed out waiting for TRB_EV_TRANSFER.\n");
    xhci_update_event_dq(xhci);
    return ret;
}
