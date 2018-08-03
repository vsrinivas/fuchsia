// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb.h>
#include <ddk/usb-request.h>

#include "xhci-trb.h"

// cache is coherent on x86
// state for endpoint's current transfer
typedef struct {
    phys_iter_t         phys_iter;
    uint32_t            packet_count;       // remaining packets to send
    uint8_t             direction;
    bool                needs_data_event;   // true if we still need to queue data event TRB
    bool                needs_status;       // true if we still need to queue status TRB
    bool                needs_transfer_trb; // true if we still need to queue transfer TRB
    bool                needs_zlp;          // true if we still need to queue a zero length packet
} xhci_transfer_state_t;

void xhci_print_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb);

// Before calling this, you should call usb_request_physmap.
void xhci_transfer_state_init(xhci_transfer_state_t* state, usb_request_t* req,
                              uint8_t ep_type, uint16_t ep_max_packet_size);

// Queues TRBs on the given transfer ring for the Data stage of a USB transfer.
// Whether TRBs will be queued depends on the given transfer state, and whether
// there is space available on the transfer ring.
//
// Returns ZX_OK if all necessary TRBs have been queued, or ZX_ERR_SHOULD_WAIT
// if the user should call this function again later.
zx_status_t xhci_queue_data_trbs(xhci_transfer_ring_t* ring, xhci_transfer_state_t* state,
                                 usb_request_t* req, int interrupter_target, bool isochronous);
