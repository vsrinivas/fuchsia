// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#include "xhci.h"

typedef struct xhci xhci_t;

typedef void (*xhci_transfer_complete_cb)(mx_status_t result, void* data);

mx_status_t xhci_queue_transfer(xhci_t* xhci, iotxn_t* txn);
int xhci_control_request(xhci_t* xhci, uint32_t slot_id, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length);
mx_status_t xhci_get_descriptor(xhci_t* xhci, uint32_t slot_id, uint8_t type, uint16_t value,
                                uint16_t index, void* data, uint16_t length);
void xhci_handle_transfer_event(xhci_t* xhci, xhci_trb_t* trb);

mx_status_t xhci_reset_endpoint(xhci_t* xhci, uint32_t slot_id, uint32_t endpoint);
mx_status_t xhci_cancel_transfers(xhci_t* xhci, uint32_t slot_id, uint32_t endpoint);
