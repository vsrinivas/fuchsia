// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/completion.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hub.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <stdbool.h>
#include <threads.h>

#include "xhci-hw.h"
#include "xhci-root-hub.h"
#include "xhci-trb.h"

#define COMMAND_RING_SIZE 8
#define EVENT_RING_SIZE 64
#define TRANSFER_RING_SIZE 64
#define ERST_ARRAY_SIZE 1

#define XHCI_RH_USB_2 0 // index of USB 2.0 virtual root hub device
#define XHCI_RH_USB_3 1 // index of USB 2.0 virtual root hub device
#define XHCI_RH_COUNT 2 // number of virtual root hub devices

typedef struct xhci_slot {
    xhci_slot_context_t* sc;
    // epcs point into DMA memory past sc
    xhci_endpoint_context_t* epcs[XHCI_NUM_EPS];
    xhci_transfer_ring_t transfer_rings[XHCI_NUM_EPS];
    uint32_t hub_address;
    uint32_t port;
    uint32_t rh_port;
    usb_speed_t speed;
} xhci_slot_t;

typedef struct xhci xhci_t;

typedef void (*xhci_command_complete_cb)(void* data, uint32_t cc, xhci_trb_t* command_trb,
                                         xhci_trb_t* event_trb);

typedef struct {
    xhci_command_complete_cb callback;
    void* data;
} xhci_command_context_t;

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

    size_t page_size;
    size_t max_slots;
    size_t max_interruptors;
    size_t context_size;
    // true if controller supports large ESIT payloads
    bool large_esit;

    // total number of ports for the root hub
    uint32_t rh_num_ports;

    // state for virtual root hub devices
    // one for USB 2.0 and the other for USB 3.0
    xhci_root_hub_t root_hubs[XHCI_RH_COUNT];

    // Maps root hub port index to the index of their virtual root hub
    uint8_t* rh_map;

    // Maps root hub port index to index relative to their virtual root hub
    uint8_t* rh_port_map;

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

    // for xhci_get_current_frame()
    mtx_t mfindex_mutex;
    // number of times mfindex has wrapped
    uint64_t mfindex_wrap_count;
   // time of last mfindex wrap
    mx_time_t last_mfindex_wrap;
};

mx_status_t xhci_init(xhci_t* xhci, void* mmio);
void xhci_start(xhci_t* xhci);
void xhci_handle_interrupt(xhci_t* xhci, bool legacy);
void xhci_post_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits,
                       xhci_command_context_t* context);
void xhci_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected);

// returns monotonically increasing frame count
uint64_t xhci_get_current_frame(xhci_t* xhci);

uint8_t xhci_endpoint_index(uint8_t ep_address);

// returns index into xhci->root_hubs[], or -1 if not a root hub
int xhci_get_root_hub_index(xhci_t* xhci, uint32_t device_id);

inline bool xhci_is_root_hub(xhci_t* xhci, uint32_t device_id) {
    return xhci_get_root_hub_index(xhci, device_id) >= 0;
}

// upper layer routines in usb-xhci.c
void* xhci_malloc(xhci_t* xhci, size_t size);
void* xhci_memalign(xhci_t* xhci, size_t alignment, size_t size);
void xhci_free(xhci_t* xhci, void* addr);
void xhci_free_phys(xhci_t* xhci, mx_paddr_t addr);
mx_paddr_t xhci_virt_to_phys(xhci_t* xhci, mx_vaddr_t addr);
mx_vaddr_t xhci_phys_to_virt(xhci_t* xhci, mx_paddr_t addr);

mx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed);
void xhci_remove_device(xhci_t* xhci, int slot_id);
void xhci_process_deferred_txns(xhci_t* xhci, xhci_transfer_ring_t* ring, bool closed);
