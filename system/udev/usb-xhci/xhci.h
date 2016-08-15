// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/completion.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb-hub.h>
#include <hw/usb.h>
#include <magenta/types.h>
#include <stdbool.h>
#include <system/compiler.h>
#include <system/listnode.h>
#include <threads.h>

#include "xhci-hw.h"

#define COMMAND_RING_SIZE 8
#define EVENT_RING_SIZE 64
#define TRANSFER_RING_SIZE 64
#define ERST_ARRAY_SIZE 1

// used for both command ring and transfer rings
typedef struct xhci_transfer_ring {
    xhci_trb_t* start;
    xhci_trb_t* current;
    uint8_t pcs; // producer cycle status

    mtx_t mutex;
    list_node_t pending_requests;
    completion_t completion; // signaled when pending_requests is empty
    bool dead;
} xhci_transfer_ring_t;

typedef struct xhci_event_ring {
    xhci_trb_t* start;
    xhci_trb_t* current;
    xhci_trb_t* end;
    erst_entry_t* erst_array;
    uint8_t ccs; // consumer cycle status
} xhci_event_ring_t;

typedef struct xhci_slot {
    xhci_slot_context_t* sc;
    // epcs point into DMA memory past sc
    xhci_endpoint_context_t* epcs[XHCI_NUM_EPS];
    xhci_transfer_ring_t transfer_rings[XHCI_NUM_EPS];
    uint32_t hub_address;
    uint32_t port;
    uint32_t rh_port;
    usb_speed_t speed;
    bool enabled;
} xhci_slot_t;

typedef struct xhci xhci_t;

typedef void (*xhci_command_complete_cb)(void* data, uint32_t cc, xhci_trb_t* command_trb,
                                         xhci_trb_t* event_trb);

typedef struct {
    xhci_command_complete_cb callback;
    void* data;
} xhci_command_context_t;

typedef void (*xhci_transfer_complete_cb)(mx_status_t result, void* data);

typedef struct {
    xhci_transfer_complete_cb callback;
    void* data;

    // transfer ring we are queued on
    xhci_transfer_ring_t* transfer_ring;
    // for transfer ring's list of pending requests
    list_node_t node;
} xhci_transfer_context_t;

struct xhci {
    // MMIO data structures
    xhci_cap_regs_t* cap_regs;
    xhci_op_regs_t* op_regs;
    volatile uint32_t* doorbells;
    xhci_runtime_regs_t* runtime_regs;

    // DMA data structures
    uint64_t* dcbaa;
    uint64_t* scratch_pad;

    xhci_transfer_ring_t command_ring;
    xhci_command_context_t* command_contexts[COMMAND_RING_SIZE];

    // One event ring for now, but we will have multiple if we use multiple interruptors
    xhci_event_ring_t event_rings[1];

    size_t max_slots;
    size_t max_interruptors;
    size_t context_size;

    // Root hub state
    uint32_t rh_num_ports;

    // device thread stuff
    thrd_t device_thread;
    xhci_slot_t* slots;

    // for command processing in xhci-device-manager.c
    list_node_t command_queue;
    mtx_t command_queue_mutex;
    completion_t command_queue_completion;

    // DMA buffers used by xhci_device_thread in xhci-device-manager.c
    uint8_t* input_context;
    usb_device_descriptor_t* device_descriptor;
    usb_configuration_descriptor_t* config_descriptor;
};

mx_status_t xhci_init(xhci_t* xhci, void* mmio);
void xhci_start(xhci_t* xhci);
void xhci_handle_interrupt(xhci_t* xhci, bool legacy);
void xhci_post_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits,
                       xhci_command_context_t* context);

uint32_t xhci_endpoint_index(usb_endpoint_descriptor_t* ep);

// xhci-device-manager.c
mx_status_t xhci_enumerate_device(xhci_t* xhci, uint32_t hub_address, uint32_t port, usb_speed_t speed);
mx_status_t xhci_device_disconnected(xhci_t* xhci, uint32_t hub_address, uint32_t port);
void xhci_start_device_thread(xhci_t* xhci);
mx_status_t xhci_configure_hub(xhci_t* xhci, int slot_id, usb_speed_t speed,
                               usb_hub_descriptor_t* descriptor);
mx_status_t xhci_rh_port_connected(xhci_t* xhci, uint32_t port);
mx_status_t xhci_rh_port_disconnected(xhci_t* xhci, uint32_t port);

// xhci-root-hub.c
void xhci_handle_port_changed_event(xhci_t* xhci, xhci_trb_t* trb);
void xhci_handle_rh_port_connected(xhci_t* xhci, int port);

// xhci-transfer.c
mx_status_t xhci_queue_transfer(xhci_t* xhci, int slot_id, usb_setup_t* setup, void* data,
                                uint16_t length, int ep, int direction, xhci_transfer_context_t* context);
mx_status_t xhci_control_request(xhci_t* xhci, int slot_id, uint8_t request_type, uint8_t request,
                                 uint16_t value, uint16_t index, void* data, uint16_t length);
void xhci_cancel_transfers(xhci_t* xhci, xhci_transfer_ring_t* ring);
mx_status_t xhci_get_descriptor(xhci_t* xhci, int slot_id, uint8_t type, uint16_t value,
                                uint16_t index, void* data, uint16_t length);
void xhci_handle_transfer_event(xhci_t* xhci, xhci_trb_t* trb);

// xhci-trb.c
mx_status_t xhci_transfer_ring_init(xhci_t* xhci, xhci_transfer_ring_t* tr, int count);
void xhci_transfer_ring_free(xhci_t* xhci, xhci_transfer_ring_t* ring);
mx_status_t xhci_event_ring_init(xhci_t* xhci, int interruptor, int count);
void xhci_clear_trb(xhci_trb_t* trb);
void* xhci_read_trb_ptr(xhci_t* xhci, xhci_trb_t* trb);
xhci_trb_t* xhci_get_next_trb(xhci_t* xhci, xhci_trb_t* trb);
void xhci_increment_ring(xhci_t* xhci, xhci_transfer_ring_t* ring);

// physical memory management
void* xhci_malloc(xhci_t* xhci, size_t size);
void* xhci_memalign(xhci_t* xhci, size_t alignment, size_t size);
void xhci_free(xhci_t* xhci, void* addr);
void xhci_free_phys(xhci_t* xhci, mx_paddr_t addr);
mx_paddr_t xhci_virt_to_phys(xhci_t* xhci, mx_vaddr_t addr);
mx_vaddr_t xhci_phys_to_virt(xhci_t* xhci, mx_paddr_t addr);

mx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int speed,
                            usb_device_descriptor_t* device_descriptor,
                            usb_configuration_descriptor_t** config_descriptors);
void xhci_remove_device(xhci_t* xhci, int slot_id);
