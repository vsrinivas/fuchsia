// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/usb-dci.h>
#include <magenta/listnode.h>
#include <magenta/types.h>
#include <magenta/hw/usb.h>

#include <threads.h>

#include "dwc3-types.h"

// physical endpoint numbers for ep0
#define EP0_OUT             0
#define EP0_IN              1
#define EP0_FIFO            EP0_OUT

#define EP_OUT(ep_num)      (((ep_num) & 1) == 0)
#define EP_IN(ep_num)       (((ep_num) & 1) == 1)

#define EVENT_BUFFER_SIZE   PAGE_SIZE
#define EP0_MAX_PACKET_SIZE 512
#define DWC3_MAX_EPS    32

// converts a USB endpoint address to 0 - 31 index
#define dwc3_ep_num(addr) ((((addr) & 0xF) << 1) | !!((addr) & USB_DIR_IN))

typedef enum {
    EP0_STATE_NONE,
    EP0_STATE_SETUP,            // Queued setup phase
    EP0_STATE_DATA_OUT,         // Queued data on EP0_OUT
    EP0_STATE_DATA_IN,          // Queued data on EP0_IN
    EP0_STATE_WAIT_NRDY_OUT,    // Waiting for not-ready on EP0_OUT
    EP0_STATE_WAIT_NRDY_IN,     // Waiting for not-ready on EP0_IN
    EP0_STATE_STATUS,           // Waiting for status to complete
} dwc3_ep0_state;

typedef struct {
    io_buffer_t buffer;
    dwc3_trb_t* first;      // first TRB in the fifo
    dwc3_trb_t* next;       // next free TRB in the fifo
    dwc3_trb_t* current;    // TRB for currently pending transaction
    dwc3_trb_t* last;       // last TRB in the fifo (link TRB)
} dwc3_fifo_t;

typedef struct {
    dwc3_fifo_t fifo;
    list_node_t queued_txns;    // iotxns waiting to be processed
    iotxn_t* current_txn;       // iotxn currently being processed
    unsigned rsrc_id;           // resource ID for current_txn

    // Used for synchronizing endpoint state
    // and ep specific hardware registers
    // This should be acquired before dwc3_t.lock
    // if acquiring both locks.
    mtx_t lock;

    uint16_t max_packet_size;
    uint8_t ep_num;
    bool enabled;
    uint8_t type;           // control, bulk, interrupt or isochronous
    uint8_t interval;
    // TODO(voydanoff) USB 3 specific stuff here

    bool got_not_ready;
    bool stalled;
} dwc3_endpoint_t;

typedef struct {
    mx_device_t* mxdev;
    usb_dci_interface_t dci_intf;
    pdev_mmio_buffer_t mmio;

    // event stuff
    io_buffer_t event_buffer;
    mx_handle_t irq_handle;
    thrd_t irq_thread;

    dwc3_endpoint_t eps[DWC3_MAX_EPS];

    // connection state
    usb_speed_t speed;

    // ep0 stuff
    usb_setup_t cur_setup;      // current setup request
    io_buffer_t ep0_buffer;
    dwc3_ep0_state ep0_state;

    // Used for synchronizing global state
    // and non ep specific hardware registers.
    // dwc3_endpoint_t.lock should be acquired first
    // if acquiring both locks.
    mtx_t lock;
    bool configured;
} dwc3_t;

static inline volatile void* dwc3_mmio(dwc3_t* dwc) {
    return dwc->mmio.vaddr;
}

void dwc3_usb_reset(dwc3_t* dwc);
void dwc3_disconnected(dwc3_t* dwc);
void dwc3_connection_done(dwc3_t* dwc);
void dwc3_set_address(dwc3_t* dwc, unsigned address);
void dwc3_reset_configuration(dwc3_t* dwc);

// Commands
void dwc3_cmd_start_new_config(dwc3_t* dwc, unsigned ep_num, unsigned resource_index);
void dwc3_cmd_ep_set_config(dwc3_t* dwc, unsigned ep_num, unsigned ep_type,
                            unsigned max_packet_size, unsigned interval, bool modify);
void dwc3_cmd_ep_transfer_config(dwc3_t* dwc, unsigned ep_num);
void dwc3_cmd_ep_start_transfer(dwc3_t* dwc, unsigned ep_num, mx_paddr_t trb_phys);
void dwc3_cmd_ep_end_transfer(dwc3_t* dwc, unsigned ep_num);
void dwc3_cmd_ep_set_stall(dwc3_t* dwc, unsigned ep_num);
void dwc3_cmd_ep_clear_stall(dwc3_t* dwc, unsigned ep_num);

// Endpoints
mx_status_t dwc3_ep_fifo_init(dwc3_t* dwc, unsigned ep_num);
void dwc3_ep_fifo_release(dwc3_t* dwc, unsigned ep_num);
mx_status_t dwc3_ep_config(dwc3_t* dwc, usb_endpoint_descriptor_t* ep_desc,
                                  usb_ss_ep_comp_descriptor_t* ss_comp_desc);
void dwc3_ep_set_config(dwc3_t* dwc, unsigned ep_num, bool enable);
mx_status_t dwc3_ep_disable(dwc3_t* dwc, uint8_t ep_addr);
void dwc3_start_eps(dwc3_t* dwc);
void dwc3_ep_queue(dwc3_t* dwc, unsigned ep_num, iotxn_t* txn);
void dwc3_ep_start_transfer(dwc3_t* dwc, unsigned ep_num, unsigned type, mx_paddr_t buffer,
                            size_t length);
void dwc3_ep_xfer_started(dwc3_t* dwc, unsigned ep_num, unsigned rsrc_id);
void dwc3_ep_xfer_complete(dwc3_t* dwc, unsigned ep_num);
void dwc3_ep_xfer_not_ready(dwc3_t* dwc, unsigned ep_num, unsigned stage);
mx_status_t dwc3_ep_set_stall(dwc3_t* dwc, unsigned ep_num, bool stall);
void dwc3_ep_end_transfers(dwc3_t* dwc, unsigned ep_num, mx_status_t reason);

// Endpoint 0
mx_status_t dwc3_ep0_init(dwc3_t* dwc);
void dwc3_ep0_reset(dwc3_t* dwc);
void dwc3_ep0_start(dwc3_t* dwc);
void dwc3_ep0_xfer_not_ready(dwc3_t* dwc, unsigned ep_num, unsigned stage);
void dwc3_ep0_xfer_complete(dwc3_t* dwc, unsigned ep_num);


// Events
void dwc3_events_start(dwc3_t* dwc);

// Utils
void dwc3_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected);
void dwc3_print_status(dwc3_t* dwc);
