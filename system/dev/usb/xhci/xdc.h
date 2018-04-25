// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

#include "xdc-hw.h"
#include "xhci-trb.h"

// The type and length fields for a string descriptor are one byte each.
#define STR_DESC_METADATA_LEN  2
#define MAX_STR_LEN            64

// There are only two endpoints, one for bulk OUT and one for bulk IN.
#define OUT_EP_IDX             0
#define IN_EP_IDX              1
#define NUM_EPS                2

typedef struct {
    xhci_transfer_ring_t transfer_ring;
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
} xdc_t;

// TODO(jocelyndang): we should get our own handles rather than borrowing them from XHCI.
zx_status_t xdc_bind(zx_device_t* parent, zx_handle_t bti_handle, void* mmio);