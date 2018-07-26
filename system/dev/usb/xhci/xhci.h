// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/sync/completion.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb-hub.h>
#include <zircon/types.h>
#include <zircon/listnode.h>
#include <limits.h>
#include <stdbool.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/usb-request.h>

#include "xhci-hw.h"
#include "xhci-root-hub.h"
#include "xhci-transfer-common.h"
#include "xhci-trb.h"

// choose ring sizes to allow each ring to fit in a single page
#define COMMAND_RING_SIZE (PAGE_SIZE / sizeof(xhci_trb_t))
#define TRANSFER_RING_SIZE (PAGE_SIZE / sizeof(xhci_trb_t))
#define EVENT_RING_SIZE (PAGE_SIZE / sizeof(xhci_trb_t))
#define ERST_ARRAY_SIZE 1

#define XHCI_RH_USB_2 0 // index of USB 2.0 virtual root hub device
#define XHCI_RH_USB_3 1 // index of USB 2.0 virtual root hub device
#define XHCI_RH_COUNT 2 // number of virtual root hub devices

#define ISOCH_INTERRUPTER 1

#if __x86_64__
// cache is coherent on x86, so we always use cached buffers
#define XHCI_IO_BUFFER_UNCACHED 0
#else
#define XHCI_IO_BUFFER_UNCACHED IO_BUFFER_UNCACHED
#endif

typedef enum {
    EP_STATE_DEAD = 0, // device does not exist or has been removed
    EP_STATE_RUNNING,
    EP_STATE_HALTED,   // halted due to stall
    EP_STATE_PAUSED,   // temporarily stopped for canceling a transfer
    EP_STATE_DISABLED, // endpoint is not enabled
    EP_STATE_ERROR,    // endpoint has error condition
} xhci_ep_state_t;

typedef struct {
    const xhci_endpoint_context_t* epc;
    xhci_transfer_ring_t transfer_ring;
    list_node_t queued_reqs;     // requests waiting to be processed
    usb_request_t* current_req;  // request currently being processed
    list_node_t pending_reqs;    // processed requests waiting for completion, including current_req
    xhci_transfer_state_t* transfer_state;  // transfer state for current_req
    mtx_t lock;
    xhci_ep_state_t state;
    uint16_t max_packet_size;
    uint8_t ep_type;
} xhci_endpoint_t;

typedef struct xhci_slot {
    // buffer for our device context
    io_buffer_t buffer;
    const xhci_slot_context_t* sc;
    // epcs point into DMA memory past sc
    xhci_endpoint_t eps[XHCI_NUM_EPS];
    usb_request_t* current_ctrl_req;
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

typedef enum {
    XHCI_PCI_LEGACY,
    XHCI_PCI_MSI,
    XHCI_PDEV,
} xhci_mode_t;

struct xhci {
    // the device we implement
    zx_device_t* zxdev;

    // interface for calling back to usb bus driver
    usb_bus_interface_t bus;

    xhci_mode_t mode;
    atomic_bool suspended;

    // Desired number of interrupters. This may be greater than what is
    // supported by hardware. The actual number of interrupts configured
    // will not exceed this, and is stored in num_interrupts.
#define INTERRUPTER_COUNT 2
    thrd_t completer_threads[INTERRUPTER_COUNT];
    zx_handle_t irq_handles[INTERRUPTER_COUNT];
    // actual number of interrupts we are using
    uint32_t num_interrupts;

    zx_handle_t mmio_handle;
    void* mmio;
    size_t mmio_size;

    // PCI support
    pci_protocol_t pci;
    zx_handle_t cfg_handle;

    // platform device support
    platform_device_protocol_t* pdev;

    // MMIO data structures
    xhci_cap_regs_t* cap_regs;
    xhci_op_regs_t* op_regs;
    volatile uint32_t* doorbells;
    xhci_runtime_regs_t* runtime_regs;

    // DMA data structures
    uint64_t* dcbaa;
    zx_paddr_t dcbaa_phys;

    xhci_transfer_ring_t command_ring;
    mtx_t command_ring_lock;
    xhci_command_context_t* command_contexts[COMMAND_RING_SIZE];

    // Each interrupter has an event ring.
    // Only indices up to num_interrupts will be populated.
    xhci_event_ring_t event_rings[INTERRUPTER_COUNT];
    erst_entry_t* erst_arrays[INTERRUPTER_COUNT];
    zx_paddr_t erst_arrays_phys[INTERRUPTER_COUNT];

    size_t page_size;
    size_t max_slots;
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

    // Pointer to the USB Legacy Support Capability, if present.
    xhci_usb_legacy_support_cap_t* usb_legacy_support_cap;

    // device thread stuff
    thrd_t device_thread;
    xhci_slot_t* slots;

    // for command processing in xhci-device-manager.c
    list_node_t command_queue;
    mtx_t command_queue_mutex;
    sync_completion_t command_queue_completion;

    // DMA buffers used by xhci_device_thread in xhci-device-manager.c
    uint8_t* input_context;
    zx_paddr_t input_context_phys;
    mtx_t input_context_lock;

    // for xhci_get_current_frame()
    mtx_t mfindex_mutex;
    // number of times mfindex has wrapped
    uint64_t mfindex_wrap_count;
   // time of last mfindex wrap
    zx_time_t last_mfindex_wrap;

    // VMO buffer for DCBAA and ERST array
    io_buffer_t dcbaa_erst_buffer;
    // VMO buffer for input context
    io_buffer_t input_context_buffer;
    // VMO buffer for scratch pad pages
    io_buffer_t scratch_pad_pages_buffer;
    // VMO buffer for scratch pad index
    io_buffer_t scratch_pad_index_buffer;

    zx_handle_t bti_handle;

    // pool of control requests that can be reused
    usb_request_pool_t free_reqs;
};

zx_status_t xhci_init(xhci_t* xhci, xhci_mode_t mode, uint32_t num_interrupts);
// Returns the max number of interrupters supported by the xhci.
// This is different to xhci->num_interrupts.
uint32_t xhci_get_max_interrupters(xhci_t* xhci);
int xhci_get_slot_ctx_state(xhci_slot_t* slot);
int xhci_get_ep_ctx_state(xhci_slot_t* slot, xhci_endpoint_t* ep);
void xhci_set_dbcaa(xhci_t* xhci, uint32_t slot_id, zx_paddr_t paddr);
zx_status_t xhci_start(xhci_t* xhci);
void xhci_handle_interrupt(xhci_t* xhci, uint32_t interrupter);
void xhci_post_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits,
                       xhci_command_context_t* context);
void xhci_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected);
void xhci_wait_bits64(volatile uint64_t* ptr, uint64_t bits, uint64_t expected);

void xhci_stop(xhci_t* xhci);
void xhci_free(xhci_t* xhci);

// returns monotonically increasing frame count
uint64_t xhci_get_current_frame(xhci_t* xhci);

uint8_t xhci_endpoint_index(uint8_t ep_address);

// returns index into xhci->root_hubs[], or -1 if not a root hub
int xhci_get_root_hub_index(xhci_t* xhci, uint32_t device_id);

static inline bool xhci_is_root_hub(xhci_t* xhci, uint32_t device_id) {
    return xhci_get_root_hub_index(xhci, device_id) >= 0;
}

// upper layer routines in usb-xhci.c
zx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed);
void xhci_remove_device(xhci_t* xhci, int slot_id);

void xhci_request_queue(xhci_t* xhci, usb_request_t* req);
