// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XDC_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XDC_H_

#include <lib/ddk/device.h>
#include <lib/sync/completion.h>
#include <threads.h>

#include <atomic>

#include <usb/usb-request.h>
#include <xdc-server-utils/packet.h>

#include "xdc-hw.h"
#include "xhci-transfer-common.h"
#include "xhci-trb.h"

namespace usb_xhci {

// The type and length fields for a string descriptor are one byte each.
#define STR_DESC_METADATA_LEN 2
#define MAX_STR_LEN 64

// There are only two endpoints, one for bulk OUT and one for bulk IN.
#define OUT_EP_IDX 0
#define IN_EP_IDX 1
#define NUM_EPS 2

// See XHCI Spec, 7.6.3.2
#define EP_CTX_MAX_PACKET_SIZE 1024

#define MAX_EP_DEBUG_NAME_LEN 4

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
  uint8_t direction;                     // USB_DIR_OUT or USB_DIR_IN

  xdc_ep_state_t state;
  bool got_err_event;  // encountered a TRB error on the event ring

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

// This is used by the xdc_poll thread to monitors changes in the debug capability register state,
// and handle completed requests.
// TODO(jocelyndang): move this and all poll thread related functions into a single file.
typedef struct {
  // Whether a Root Hub Port is connected to a Debug Host and assigned to the Debug Capability.
  bool connected;
  // The last connection time in nanoseconds, with respect to the monotonic clock.
  zx_time_t last_conn;

  // Whether the Debug Device is in the Configured state.
  // Changes to this are also copied to the xdc struct configured mmember.
  bool configured;

  bool halt_in;
  bool halt_out;

  // Requests that need their complete_cb called.
  list_node_t completed_reqs;
} xdc_poll_state_t;

typedef struct {
  zx_device_t* zxdev = nullptr;

  // Shared from XHCI.
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  void* mmio = nullptr;

  xdc_debug_cap_regs_t* debug_cap_regs = nullptr;

  // Underlying buffer for the event ring segment table
  io_buffer_t erst_buffer = {};
  erst_entry_t* erst_array = nullptr;

  xhci_event_ring_t event_ring;

  // Underlying buffer for the context data and string descriptors.
  io_buffer_t context_str_descs_buffer = {};
  xdc_context_data_t* context_data = nullptr;
  xdc_str_descs_t* str_descs = nullptr;

  thrd_t start_thread = 0;

  // Whether to suspend all activity.
  std::atomic<bool> suspended;

  xdc_endpoint_t eps[NUM_EPS];
  // Whether the Debug Device is in the Configured state.
  bool configured = false;
  // Needs to be acquired before accessing the eps and configured members.
  // TODO(jocelyndang): make these separate locks?
  mtx_t lock;

  bool writable = false;
  usb_request_pool_t free_write_reqs = {};
  mtx_t write_lock;

  list_node_t free_read_reqs = {};
  xdc_packet_state_t cur_read_packet = {};
  mtx_t read_lock;

  list_node_t instance_list = {};
  // Streams registered by the host.
  list_node_t host_streams = {};
  mtx_t instance_list_lock = {};

  // At least one xdc instance has been opened.
  sync_completion_t has_instance_completion;
  std::atomic<int> num_instances;
} xdc_t;

typedef struct {
  list_node_t node;
  usb_request_complete_t complete_cb;
  void* context;
} xdc_req_internal_t;

#define USB_REQ_TO_XDC_INTERNAL(req, size) ((xdc_req_internal_t*)((uintptr_t)(req) + (size)))
#define XDC_INTERNAL_TO_USB_REQ(ctx, size) ((usb_request_t*)((uintptr_t)(ctx) - (size)))

zx_status_t xdc_req_list_add_head(list_node_t* list, usb_request_t* req, size_t parent_req_size);
zx_status_t xdc_req_list_add_tail(list_node_t* list, usb_request_t* req, size_t parent_req_size);
usb_request_t* xdc_req_list_remove_head(list_node_t* list, size_t parent_req_size);
usb_request_t* xdc_req_list_remove_tail(list_node_t* list, size_t parent_req_size);

// TODO(jocelyndang): we should get our own handles rather than borrowing them from XHCI.
zx_status_t xdc_bind(zx_device_t* parent, zx_handle_t bti_handle, void* mmio);

void xdc_endpoint_set_halt_locked(xdc_t* xdc, xdc_poll_state_t* poll_state, xdc_endpoint_t* ep)
    __TA_REQUIRES(xdc->lock);
void xdc_write_complete(void* ctx, usb_request_t* req);
void xdc_read_complete(void* ctx, usb_request_t* req);

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XDC_H_
