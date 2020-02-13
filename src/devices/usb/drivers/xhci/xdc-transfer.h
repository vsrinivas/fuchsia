// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XDC_TRANSFER_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XDC_TRANSFER_H_

#include "xdc.h"

namespace usb_xhci {

// Restarts a stopped transfer ring. All TRBs queued on the transfer ring are
// converted to NO-OPs, and will attempt to reschedule previously pending requests.
zx_status_t xdc_restart_transfer_ring_locked(xdc_t* xdc, xdc_endpoint_t* ep)
    __TA_REQUIRES(xdc->lock);

void xdc_process_transactions_locked(xdc_t* xdc, xdc_endpoint_t* ep) __TA_REQUIRES(xdc->lock);

zx_status_t xdc_queue_transfer(xdc_t* xdc, usb_request_t* req, bool in, bool is_ctrl_msg);

bool xdc_has_free_trbs(xdc_t* xdc, bool in);

// This is called from the xdc_poll thread.
void xdc_handle_transfer_event_locked(xdc_t* xdc, xdc_poll_state_t* poll_state, xhci_trb_t* trb)
    __TA_REQUIRES(xdc->lock);

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XDC_TRANSFER_H_
