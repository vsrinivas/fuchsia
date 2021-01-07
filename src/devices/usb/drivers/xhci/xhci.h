// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_H_

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/usb/bus/c/banjo.h>
#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/profile.h>
#include <limits.h>
#include <threads.h>
#include <zircon/hw/usb.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <atomic>

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <usb/usb-request.h>

#include "trb-sizes.h"
#include "xhci-hw.h"
#include "xhci-root-hub.h"
#include "xhci-transfer-common.h"
#include "xhci-trb.h"

namespace usb_xhci {

#define XHCI_RH_USB_2 0  // index of USB 2.0 virtual root hub device
#define XHCI_RH_USB_3 1  // index of USB 2.0 virtual root hub device
#define XHCI_RH_COUNT 2  // number of virtual root hub devices

#define ISOCH_INTERRUPTER 1

#if __x86_64__
// cache is coherent on x86, so we always use cached buffers
#define XHCI_IO_BUFFER_UNCACHED 0
#else
#define XHCI_IO_BUFFER_UNCACHED IO_BUFFER_UNCACHED
#endif

enum xhci_ep_state_t {
  EP_STATE_DEAD = 0,  // device does not exist or has been removed
  EP_STATE_RUNNING,
  EP_STATE_HALTED,    // halted due to stall
  EP_STATE_PAUSED,    // temporarily stopped for canceling a transfer
  EP_STATE_DISABLED,  // endpoint is not enabled
  EP_STATE_ERROR,     // endpoint has error condition
};

struct xhci_endpoint_t {
  const xhci_endpoint_context_t* epc = nullptr;
  xhci_transfer_ring_t transfer_ring = {};
  // requests waiting to be processed
  list_node_t queued_reqs;
  // request currently being processed
  usb_request_t* current_req = nullptr;
  // processed requests waiting for completion, including current_req
  list_node_t pending_reqs;
  // transfer state for current_req
  xhci_transfer_state_t* transfer_state = nullptr;
  fbl::Mutex lock;
  xhci_ep_state_t state = EP_STATE_DEAD;
  uint16_t max_packet_size;
  uint8_t ep_type;
};

struct xhci_slot_t {
  // buffer for our device context
  io_buffer_t buffer = {};
  const xhci_slot_context_t* sc = nullptr;
  // epcs point into DMA memory past sc
  xhci_endpoint_t eps[XHCI_NUM_EPS];
  usb_request_t* current_ctrl_req = nullptr;
  uint32_t hub_address;
  uint32_t port;
  uint32_t rh_port;
  usb_speed_t speed;
};

struct xhci_usb_request_internal_t {
  // callback to the upper layer
  usb_request_complete_t complete_cb;
  // for queueing request at xhci level
  list_node_t node;
  xhci_trb_t* context;
};

#define USB_REQ_TO_XHCI_INTERNAL(req) \
  ((xhci_usb_request_internal_t*)((uintptr_t)(req) + sizeof(usb_request_t)))
#define XHCI_INTERNAL_TO_USB_REQ(ctx) ((usb_request_t*)((uintptr_t)(ctx) - sizeof(usb_request_t)))

typedef void (*xhci_command_complete_cb)(void* data, uint32_t cc, xhci_trb_t* command_trb,
                                         xhci_trb_t* event_trb);

struct xhci_command_context_t {
  xhci_command_complete_cb callback;
  xhci_trb_t* next_trb;
  void* data;
};

enum xhci_mode_t {
  XHCI_PCI,
  XHCI_PDEV,
};

static constexpr uint32_t INTERRUPTER_COUNT = 2;

struct xhci_t {
  // interface for calling back to usb bus driver
  usb_bus_interface_protocol_t bus = {};

  xhci_mode_t mode = XHCI_PCI;
  std::atomic<bool> suspended = false;

  // Desired number of interrupters. This may be greater than what is
  // supported by hardware. The actual number of interrupts configured
  // will not exceed this, and is stored in num_interrupts.
  thrd_t completer_threads[INTERRUPTER_COUNT];
  zx::interrupt irq_handles[INTERRUPTER_COUNT];
  // actual number of interrupts we are using
  uint32_t num_interrupts = 0;

  std::optional<ddk::MmioBuffer> mmio;

  // PCI support
  pci_protocol_t pci = {};

  // MMIO data structures
  xhci_cap_regs_t* cap_regs = nullptr;
  xhci_op_regs_t* op_regs = nullptr;
  volatile uint32_t* doorbells = nullptr;
  xhci_runtime_regs_t* runtime_regs = nullptr;

  // DMA data structures
  uint64_t* dcbaa = nullptr;
  zx_paddr_t dcbaa_phys = 0;

  xhci_transfer_ring_t command_ring = {};
  fbl::Mutex command_ring_lock;
  xhci_command_context_t* command_contexts[COMMAND_RING_SIZE] = {};

  // Each interrupter has an event ring.
  // Only indices up to num_interrupts will be populated.
  xhci_event_ring_t event_rings[INTERRUPTER_COUNT];
  erst_entry_t* erst_arrays[INTERRUPTER_COUNT] = {};
  zx_paddr_t erst_arrays_phys[INTERRUPTER_COUNT] = {};

  size_t page_size = 0;
  uint32_t max_slots = 0;
  size_t context_size = 0;
  // true if controller supports large ESIT payloads
  bool large_esit = false;

  // total number of ports for the root hub
  uint8_t rh_num_ports = 0;

  // state for virtual root hub devices
  // one for USB 2.0 and the other for USB 3.0
  xhci_root_hub_t root_hubs[XHCI_RH_COUNT];

  // Maps root hub port index to the index of their virtual root hub
  fbl::Array<uint8_t> rh_map;

  // Maps root hub port index to index relative to their virtual root hub
  fbl::Array<uint8_t> rh_port_map;

  // Pointer to the USB Legacy Support Capability, if present.
  xhci_usb_legacy_support_cap_t* usb_legacy_support_cap = nullptr;

  // device thread stuff
  thrd_t device_thread = 0;
  fbl::Array<xhci_slot_t> slots;

  // for command processing in xhci-device-manager.c
  list_node_t command_queue;
  fbl::Mutex command_queue_mutex;
  sync_completion_t command_queue_completion;

  // DMA buffers used by xhci_device_thread in xhci-device-manager.c
  uint8_t* input_context = nullptr;
  zx_paddr_t input_context_phys = 0;
  fbl::Mutex input_context_lock;

  // for xhci_get_current_frame()
  fbl::Mutex mfindex_mutex;
  // number of times mfindex has wrapped
  uint64_t mfindex_wrap_count = 0;
  // time of last mfindex wrap
  zx_time_t last_mfindex_wrap = 0;

  // VMO buffer for DCBAA and ERST array
  ddk::IoBuffer dcbaa_erst_buffer;
  ddk::IoBuffer erst_buffers[INTERRUPTER_COUNT];
  size_t erst_sizes[INTERRUPTER_COUNT];
  // VMO buffer for input context
  ddk::IoBuffer input_context_buffer;
  // VMO buffer for scratch pad pages
  ddk::IoBuffer scratch_pad_pages_buffer;
  // VMO buffer for scratch pad index
  ddk::IoBuffer scratch_pad_index_buffer;

  zx::bti bti_handle;
  zx::profile profile_handle;

  // pool of control requests that can be reused
  usb_request_pool_t free_reqs = {};
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
zx_status_t xhci_post_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits,
                              xhci_command_context_t* context);
void xhci_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected);
void xhci_wait_bits64(volatile uint64_t* ptr, uint64_t bits, uint64_t expected);

void xhci_stop(xhci_t* xhci);
void xhci_free(xhci_t* xhci);

bool xhci_add_to_list_tail(xhci_t* xhci, list_node_t* list, usb_request_t* req);
bool xhci_add_to_list_head(xhci_t* xhci, list_node_t* list, usb_request_t* req);
bool xhci_remove_from_list_head(xhci_t* xhci, list_node_t* list, usb_request_t** req);
bool xhci_remove_from_list_tail(xhci_t* xhci, list_node_t* list, usb_request_t** req);
void xhci_delete_req_node(xhci_t* xhci, usb_request_t* req);

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

void xhci_request_queue(xhci_t* xhci, usb_request_t* req,
                        const usb_request_complete_t* complete_cb);

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_H_
