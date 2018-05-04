// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/usb-request.h>

#include "xdc-hw.h"
#include "xhci-transfer-common.h"
#include "xhci-trb.h"

// The type and length fields for a string descriptor are one byte each.
#define STR_DESC_METADATA_LEN  2
#define MAX_STR_LEN            64

// There are only two endpoints, one for bulk OUT and one for bulk IN.
#define OUT_EP_IDX             0
#define IN_EP_IDX              1
#define NUM_EPS                2

// See XHCI Spec, 7.6.3.2
#define EP_CTX_MAX_PACKET_SIZE 1024

#define MAX_EP_DEBUG_NAME_LEN  4

typedef enum {
    XDC_EP_STATE_DEAD = 0,  // device does not exist or has been removed
    XDC_EP_STATE_RUNNING,   // EP is accepting TRBs on the transfer ring
    XDC_EP_STATE_HALTED,    // EP halted due to stall
    XDC_EP_STATE_STOPPED    // EP halt has been cleared, but not yet accepting TRBs
} xdc_ep_state_t;

typedef struct {
    xhci_transfer_ring_t transfer_ring;
    list_node_t queued_reqs;     // requests waiting to be processed
    usb_request_t* current_req;  // request currently being processed
    list_node_t pending_reqs;    // processed requests waiting for completion, including current_req
    xhci_transfer_state_t transfer_state;  // transfer state for current_req
    uint8_t direction;  // USB_DIR_OUT or USB_DIR_IN

    xdc_ep_state_t state;

    char name[MAX_EP_DEBUG_NAME_LEN];  // For debug printing.
} xdc_endpoint_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t string[MAX_STR_LEN];
} xdc_str_desc_t;

typedef struct {
    xdc_str_desc_t str_0_desc;
    xdc_str_desc_t manufacturer_desc;
    xdc_str_desc_t product_desc;
    xdc_str_desc_t serial_num_desc;
} xdc_str_descs_t;

typedef struct {
    zx_device_t* zxdev;

    // Shared from XHCI.
    zx_handle_t bti_handle;
    void* mmio;

    xdc_debug_cap_regs_t* debug_cap_regs;

    // Underlying buffer for the event ring segment table
    io_buffer_t erst_buffer;
    erst_entry_t* erst_array;

    xhci_event_ring_t event_ring;

    // Underlying buffer for the context data and string descriptors.
    io_buffer_t context_str_descs_buffer;
    xdc_context_data_t* context_data;
    xdc_str_descs_t* str_descs;

    xdc_endpoint_t eps[NUM_EPS];

    thrd_t start_thread;

    // Whether a Root Hub Port is connected to a Debug Host and assigned to the Debug Capability.
    bool connected;
    // The last connection time in nanoseconds, with respect to the monotonic clock.
    zx_time_t last_conn;

    // Whether the Debug Device is in the Configured state.
    bool configured;

    // Whether to suspend all activity.
    atomic_bool suspended;

    mtx_t lock;
} xdc_t;

// TODO(jocelyndang): we should get our own handles rather than borrowing them from XHCI.
zx_status_t xdc_bind(zx_device_t* parent, zx_handle_t bti_handle, void* mmio);

void xdc_update_configuration_state_locked(xdc_t* xdc) __TA_REQUIRES(xdc->lock);
void xdc_update_endpoint_state_locked(xdc_t* xdc, xdc_endpoint_t* ep) __TA_REQUIRES(xdc->lock);