// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-device-manager.h"

#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/hw/usb.h>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "xhci-root-hub.h"
#include "xhci-transfer-common.h"
#include "xhci-util.h"

namespace usb_xhci {

typedef enum {
  ENUMERATE_DEVICE,
  DISCONNECT_DEVICE,
  RESET_DEVICE,
  START_ROOT_HUBS,
  STOP_THREAD,
} xhci_command_t;

// list of devices pending result of enable slot command
// list is kept on xhci_t.command_queue
struct xhci_device_command_t {
  xhci_command_t command;
  list_node_t node;
  uint32_t hub_address;
  uint32_t port;
  usb_speed_t speed;
};

static uint32_t xhci_get_route_string(xhci_t* xhci, uint32_t hub_address, uint32_t port) {
  if (hub_address == 0) {
    return 0;
  }

  xhci_slot_t* hub_slot = &xhci->slots[hub_address];
  uint32_t route =
      XHCI_GET_BITS32(&hub_slot->sc->sc0, SLOT_CTX_ROUTE_STRING_START, SLOT_CTX_ROUTE_STRING_BITS);
  int shift = 0;
  while (shift < 20) {
    if ((route & (0xF << shift)) == 0) {
      // reached end of parent hub's route string
      route |= ((port & 0xF) << shift);
      break;
    }
    shift += 4;
  }
  return route;
}

static zx_status_t xhci_address_device(xhci_t* xhci, uint32_t slot_id, uint32_t hub_address,
                                       uint32_t port, usb_speed_t speed) {
  zxlogf(TRACE, "xhci_address_device slot_id: %d port: %d hub_address: %d speed: %d", slot_id,
         port, hub_address, speed);

  int rh_index = xhci_get_root_hub_index(xhci, hub_address);
  if (rh_index >= 0) {
    // For virtual root hub devices, real hub_address is 0
    hub_address = 0;
    // convert virtual root hub port number to real port number
    port = xhci->root_hubs[rh_index].port_map[port - 1] + 1;
  }

  xhci_slot_t* slot = &xhci->slots[slot_id];
  slot->hub_address = hub_address;
  slot->port = port;
  slot->rh_port = (hub_address == 0 ? port : xhci->slots[hub_address].rh_port);
  slot->speed = speed;

  xhci_endpoint_t* ep = &slot->eps[0];

  bool enumerating = slot->sc == nullptr;
  zx_status_t status;

  // Allocate the buffers if we haven't already. They will already exist in the case of a
  // device reset.
  if (enumerating) {
    // allocate a read-only DMA buffer for device context
    size_t dc_length = xhci->context_size * XHCI_NUM_EPS;
    status = io_buffer_init(&slot->buffer, xhci->bti_handle.get(), dc_length,
                            IO_BUFFER_RO | IO_BUFFER_CONTIG | XHCI_IO_BUFFER_UNCACHED);
    if (status != ZX_OK) {
      zxlogf(ERROR, "xhci_address_device: failed to allocate io_buffer for slot");
      return status;
    }
    status =
        xhci_transfer_ring_init(&ep->transfer_ring, xhci->bti_handle.get(), TRANSFER_RING_SIZE);
    if (status < 0)
      return status;

    fbl::AllocChecker ac;
    ep->transfer_state = new (&ac) xhci_transfer_state_t;
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    memset(ep->transfer_state, 0, sizeof(*ep->transfer_state));
    ep->ep_type = USB_ENDPOINT_CONTROL;
  }

  auto* device_context = static_cast<uint8_t*>(io_buffer_virt(&slot->buffer));
  xhci_transfer_ring_t* transfer_ring = &ep->transfer_ring;

  fbl::AutoLock al(&xhci->input_context_lock);

  auto* icc = reinterpret_cast<xhci_input_control_context_t*>(xhci->input_context);
  zx_paddr_t icc_phys = xhci->input_context_phys;
  auto* sc = reinterpret_cast<xhci_slot_context_t*>(&xhci->input_context[1 * xhci->context_size]);
  auto* ep0c =
      reinterpret_cast<xhci_endpoint_context_t*>(&xhci->input_context[2 * xhci->context_size]);
  memset(icc, 0, xhci->context_size);
  memset(sc, 0, xhci->context_size);
  memset(ep0c, 0, xhci->context_size);

  slot->sc = reinterpret_cast<xhci_slot_context_t*>(device_context);
  device_context += xhci->context_size;
  for (int i = 0; i < XHCI_NUM_EPS; i++) {
    slot->eps[i].epc = reinterpret_cast<xhci_endpoint_context_t*>(device_context);
    device_context += xhci->context_size;
  }

  // Enable slot context and ep0 context
  XHCI_WRITE32(&icc->add_context_flags, XHCI_ICC_SLOT_FLAG | XHCI_ICC_EP_FLAG(0));

  // Setup slot context
  uint32_t route_string = xhci_get_route_string(xhci, hub_address, port);
  XHCI_SET_BITS32(&sc->sc0, SLOT_CTX_ROUTE_STRING_START, SLOT_CTX_ROUTE_STRING_BITS, route_string);
  XHCI_SET_BITS32(&sc->sc0, SLOT_CTX_SPEED_START, SLOT_CTX_SPEED_BITS, speed);
  XHCI_SET_BITS32(&sc->sc0, SLOT_CTX_CONTEXT_ENTRIES_START, SLOT_CTX_CONTEXT_ENTRIES_BITS, 1);
  XHCI_SET_BITS32(&sc->sc1, SLOT_CTX_ROOT_HUB_PORT_NUM_START, SLOT_CTX_ROOT_HUB_PORT_NUM_BITS,
                  slot->rh_port);

  uint32_t mtt = 0;
  uint32_t tt_hub_slot_id = 0;
  uint32_t tt_port_number = 0;
  if (hub_address != 0 && (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL)) {
    xhci_slot_t* hub_slot = &xhci->slots[hub_address];
    tt_hub_slot_id = XHCI_GET_BITS32(&hub_slot->sc->sc2, SLOT_CTX_TT_HUB_SLOT_ID_START,
                                     SLOT_CTX_TT_HUB_SLOT_ID_BITS);
    if (tt_hub_slot_id) {
      tt_port_number = XHCI_GET_BITS32(&hub_slot->sc->sc2, SLOT_CTX_TT_PORT_NUM_START,
                                       SLOT_CTX_TT_PORT_NUM_BITS);
      mtt = XHCI_GET_BITS32(&hub_slot->sc->sc0, SLOT_CTX_MTT_START, SLOT_CTX_MTT_BITS);
    } else if (hub_slot->speed == USB_SPEED_HIGH) {
      mtt = XHCI_GET_BITS32(&hub_slot->sc->sc0, SLOT_CTX_MTT_START, SLOT_CTX_MTT_BITS);
      tt_hub_slot_id = hub_address;
      tt_port_number = port;
    }
  }

  XHCI_SET_BITS32(&sc->sc0, SLOT_CTX_MTT_START, SLOT_CTX_MTT_BITS, mtt);
  XHCI_SET_BITS32(&sc->sc2, SLOT_CTX_TT_HUB_SLOT_ID_START, SLOT_CTX_TT_HUB_SLOT_ID_BITS,
                  tt_hub_slot_id);
  XHCI_SET_BITS32(&sc->sc2, SLOT_CTX_TT_PORT_NUM_START, SLOT_CTX_TT_PORT_NUM_BITS, tt_port_number);

  // Setup endpoint context for ep0
  // If this is following a device reset, the dequeue pointer may not be the start of the ring.
  zx_paddr_t tr_dequeue = xhci_transfer_ring_current_phys(transfer_ring);

  // start off with reasonable default max packet size for ep0 based on speed
  int mps;
  switch (speed) {
    case USB_SPEED_SUPER:
      mps = 512;
      break;
    case USB_SPEED_FULL:
    case USB_SPEED_HIGH:
      mps = 64;
      break;
    case USB_SPEED_LOW:
    default:
      mps = 8;
      break;
  }

  XHCI_SET_BITS32(&ep0c->epc1, EP_CTX_CERR_START, EP_CTX_CERR_BITS, 3);  // ???
  XHCI_SET_BITS32(&ep0c->epc1, EP_CTX_EP_TYPE_START, EP_CTX_EP_TYPE_BITS, EP_CTX_EP_TYPE_CONTROL);
  XHCI_SET_BITS32(&ep0c->epc1, EP_CTX_MAX_PACKET_SIZE_START, EP_CTX_MAX_PACKET_SIZE_BITS, mps);
  XHCI_WRITE32(&ep0c->epc2, ((uint32_t)tr_dequeue & EP_CTX_TR_DEQUEUE_LO_MASK) | EP_CTX_DCS);
  XHCI_WRITE32(&ep0c->tr_dequeue_hi, (uint32_t)(tr_dequeue >> 32));
  XHCI_SET_BITS32(&ep0c->epc4, EP_CTX_AVG_TRB_LENGTH_START, EP_CTX_AVG_TRB_LENGTH_BITS, 8);  // ???

  // install our device context for the slot
  xhci_set_dbcaa(xhci, slot_id, io_buffer_phys(&slot->buffer));

  // then send the address device command
  for (int i = 0; i < 5; i++) {
    status =
        xhci_send_command(xhci, TRB_CMD_ADDRESS_DEVICE, icc_phys, (slot_id << TRB_SLOT_ID_START));
    if (status == ZX_OK) {
      break;
    } else if (status != ZX_ERR_TIMED_OUT) {
      // Don't want to get into a reset loop.
      if (enumerating) {
        usb_bus_interface_reset_port(&xhci->bus, hub_address, port, enumerating);
      }
      status = xhci_send_command(xhci, TRB_CMD_ADDRESS_DEVICE, icc_phys,
                                 ((slot_id << TRB_SLOT_ID_START) | TRB_ADDRESS_DEVICE_BSR));
      if (status != ZX_OK) {
        break;
      }
      usb_device_descriptor_t device_desc;
      // Based on XHCI spec 4.6.5, some legacy devices expect
      // device descriptor request prior to SET_ADDRESS request.
      size_t actual;
      status = xhci_get_descriptor(xhci, slot_id, USB_TYPE_STANDARD, USB_DT_DEVICE << 8, 0,
                                   &device_desc, 8, &actual);
      if (status != ZX_OK || actual != 8) {
        // try again
        continue;
      }
      switch (device_desc.bMaxPacketSize0) {
        case 8:
        case 16:
        case 32:
        case 64:
        case 255:
          if (device_desc.bDescriptorType != USB_DT_DEVICE) {
            status = ZX_ERR_IO;
          }
          break;
        default:
          status = ZX_ERR_IO;
          break;
      }
      if (status != ZX_OK) {
        // try again
        continue;
      }
      XHCI_SET_BITS32(&ep0c->epc1, EP_CTX_MAX_PACKET_SIZE_START, EP_CTX_MAX_PACKET_SIZE_BITS,
                      device_desc.bMaxPacketSize0);
      zx_nanosleep(zx_deadline_after(ZX_USEC(1000)));
      status =
          xhci_send_command(xhci, TRB_CMD_ADDRESS_DEVICE, icc_phys, (slot_id << TRB_SLOT_ID_START));
      if (status != ZX_OK) {
        break;
      }
    }
  }

  if (status == ZX_OK) {
    ep->state = EP_STATE_RUNNING;
  }
  return status;
}

#define BOUNDS_CHECK(i, min, max) (i < min ? min : (i > max ? max : i))
#define LOG2(i) (31 - __builtin_clz(i))

static int compute_interval(const usb_endpoint_descriptor_t* ep, usb_speed_t speed) {
  int ep_type = ep->bmAttributes & USB_ENDPOINT_TYPE_MASK;
  int interval = ep->bInterval;

  if (ep_type == USB_ENDPOINT_CONTROL || ep_type == USB_ENDPOINT_BULK) {
    if (speed == USB_SPEED_HIGH) {
      return LOG2(interval);
    } else {
      return 0;
    }
  }

  // now we deal with interrupt and isochronous endpoints
  // first make sure bInterval is in legal range
  if (ep_type == USB_ENDPOINT_INTERRUPT && (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL)) {
    interval = BOUNDS_CHECK(interval, 1, 255);
  } else {
    interval = BOUNDS_CHECK(interval, 1, 16);
  }

  switch (speed) {
    case USB_SPEED_LOW:
      return LOG2(interval) + 3;  // + 3 to convert 125us to 1ms
    case USB_SPEED_FULL:
      if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
        return (interval - 1) + 3;
      } else {
        return LOG2(interval) + 3;
      }
    case USB_SPEED_SUPER:
    case USB_SPEED_HIGH:
      return interval - 1;
    default:
      return 0;
  }
}

static void xhci_disable_slot(xhci_t* xhci, uint32_t slot_id) {
  xhci_send_command(xhci, TRB_CMD_DISABLE_SLOT, 0, (slot_id << TRB_SLOT_ID_START));

  zxlogf(TRACE, "cleaning up slot %d", slot_id);
  xhci_slot_t* slot = &xhci->slots[slot_id];
  for (int i = 0; i < XHCI_NUM_EPS; i++) {
    xhci_endpoint_t* ep = &slot->eps[i];
    xhci_transfer_ring_free(&ep->transfer_ring);
    delete ep->transfer_state;
    ep->transfer_state = nullptr;
    ep->state = EP_STATE_DISABLED;
  }
  io_buffer_release(&slot->buffer);
  slot->sc = nullptr;
  slot->hub_address = 0;
  slot->port = 0;
  slot->rh_port = 0;
  slot->port = USB_SPEED_UNDEFINED;
}

static zx_status_t xhci_setup_slot(xhci_t* xhci, uint32_t slot_id, uint32_t hub_address,
                                   uint32_t port, usb_speed_t speed) {
  zx_status_t result = xhci_address_device(xhci, slot_id, hub_address, port, speed);
  if (result != ZX_OK) {
    return result;
  }

  // Let SET_ADDRESS settle down
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  // read first 8 bytes of device descriptor to fetch ep0 max packet size
  usb_device_descriptor_t device_descriptor;
  size_t actual;
  for (int i = 0; i < 5; i++) {
    result = xhci_get_descriptor(xhci, slot_id, USB_TYPE_STANDARD, USB_DT_DEVICE << 8, 0,
                                 &device_descriptor, 8, &actual);
    if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
      xhci_reset_endpoint(xhci, slot_id, 0);
    } else if (result != ZX_OK) {
      // Try again. The device may be flaky or slow recovering.
      continue;
    } else {
      break;
    }
  }
  if (actual != 8) {
    zxlogf(ERROR, "xhci_handle_enable_device: xhci_get_descriptor failed: %d", result);
    return ZX_ERR_BAD_STATE;
  }

  int mps;
  mps = device_descriptor.bMaxPacketSize0;
  // enforce correct max packet size for ep0
  switch (speed) {
    case USB_SPEED_LOW:
      mps = 8;
      break;
    case USB_SPEED_FULL:
      if (mps != 8 && mps != 16 && mps != 32 && mps != 64) {
        mps = 8;
      }
      break;
    case USB_SPEED_HIGH:
      mps = 64;
      break;
    case USB_SPEED_SUPER:
      // bMaxPacketSize0 is an exponent for superspeed devices
      mps = 1 << mps;
      break;
    default:
      break;
  }

  // update the max packet size in our device context
  fbl::AutoLock al(&xhci->input_context_lock);

  xhci_input_control_context_t* icc;
  icc = reinterpret_cast<xhci_input_control_context_t*>(xhci->input_context);
  zx_paddr_t icc_phys;
  icc_phys = xhci->input_context_phys;
  xhci_endpoint_context_t* ep0c;
  ep0c = reinterpret_cast<xhci_endpoint_context_t*>(&xhci->input_context[2 * xhci->context_size]);
  memset(icc, 0, xhci->context_size);
  memset(ep0c, 0, xhci->context_size);

  XHCI_WRITE32(&icc->add_context_flags, XHCI_ICC_EP_FLAG(0));
  XHCI_SET_BITS32(&ep0c->epc1, EP_CTX_MAX_PACKET_SIZE_START, EP_CTX_MAX_PACKET_SIZE_BITS, mps);

  result = xhci_send_command(xhci, TRB_CMD_EVAL_CONTEXT, icc_phys, (slot_id << TRB_SLOT_ID_START));
  if (result != ZX_OK) {
    zxlogf(ERROR, "xhci_handle_enable_device: TRB_CMD_EVAL_CONTEXT failed");
  }
  return result;
}

static zx_status_t xhci_handle_enumerate_device(xhci_t* xhci, uint32_t hub_address, uint32_t port,
                                                usb_speed_t speed) {
  zxlogf(TRACE, "xhci_handle_enumerate_device hub_address:%d port %d", hub_address, port);
  zx_status_t result = ZX_OK;
  uint32_t slot_id = 0;

  xhci_sync_command_t command;
  xhci_sync_command_init(&command);
  result = xhci_post_command(xhci, TRB_CMD_ENABLE_SLOT, 0, 0, &command.context);
  if (result != ZX_OK) {
    return result;
  }

  int cc = xhci_sync_command_wait(&command);
  if (cc == TRB_CC_SUCCESS) {
    slot_id = xhci_sync_command_slot_id(&command);
  } else {
    zxlogf(ERROR, "xhci_handle_enumerate_device: unable to get a slot");
    return ZX_ERR_NO_RESOURCES;
  }

  result = xhci_setup_slot(xhci, slot_id, hub_address, port, speed);
  if (result != ZX_OK) {
    goto disable_slot_exit;
  }

  xhci_add_device(xhci, slot_id, hub_address, speed);
  return ZX_OK;

disable_slot_exit:
  xhci_disable_slot(xhci, slot_id);
  zxlogf(ERROR, "xhci_handle_enumerate_device failed %d", result);
  return result;
}

static zx_status_t xhci_free_endpoint_state(xhci_t* xhci, xhci_slot_t* slot, xhci_endpoint_t* ep,
                                            zx_status_t complete_status) {
  xhci_transfer_ring_t* transfer_ring = &ep->transfer_ring;

  {
    fbl::AutoLock al(&ep->lock);
    if (ep->state != EP_STATE_DISABLED && ep->state != EP_STATE_DEAD) {
      return ZX_ERR_BAD_STATE;
    }
  }

  delete ep->transfer_state;
  ep->transfer_state = nullptr;
  xhci_transfer_ring_free(transfer_ring);

  // complete any remaining requests
  usb_request_t* req = nullptr;
  xhci_usb_request_internal_t* req_int = nullptr;
  while ((req_int = list_remove_head_type(&ep->pending_reqs, xhci_usb_request_internal_t, node)) !=
         nullptr) {
    req = XHCI_INTERNAL_TO_USB_REQ(req_int);
    usb_request_complete(req, complete_status, 0, &req_int->complete_cb);
  }
  while ((req_int = list_remove_head_type(&ep->queued_reqs, xhci_usb_request_internal_t, node)) !=
         nullptr) {
    req = XHCI_INTERNAL_TO_USB_REQ(req_int);
    usb_request_complete(req, complete_status, 0, &req_int->complete_cb);
  }

  return ZX_OK;
}

static zx_status_t xhci_stop_endpoint(xhci_t* xhci, uint32_t slot_id, int ep_index,
                                      xhci_ep_state_t new_state, zx_status_t complete_status) {
  xhci_slot_t* slot = &xhci->slots[slot_id];
  xhci_endpoint_t* ep = &slot->eps[ep_index];

  if (new_state != EP_STATE_DISABLED && new_state != EP_STATE_DEAD) {
    ZX_DEBUG_ASSERT_MSG(false, "xhci_stop_endpoint: bad state argument %d\n", new_state);
    return ZX_ERR_INTERNAL;
  }

  {
    fbl::AutoLock al(&ep->lock);
    if (ep->state != EP_STATE_RUNNING) {
      return ZX_ERR_BAD_STATE;
    }
    ep->state = new_state;
  }

  xhci_sync_command_t command;
  xhci_sync_command_init(&command);
  // command expects device context index, so increment ep_index by 1
  uint32_t control = (slot_id << TRB_SLOT_ID_START) | ((ep_index + 1) << TRB_ENDPOINT_ID_START);
  zx_status_t result = xhci_post_command(xhci, TRB_CMD_STOP_ENDPOINT, 0, control, &command.context);
  if (result != ZX_OK) {
    return result;
  }

  int cc = xhci_sync_command_wait(&command);
  if (cc != TRB_CC_SUCCESS && cc != TRB_CC_CONTEXT_STATE_ERROR) {
    // TRB_CC_CONTEXT_STATE_ERROR is normal here in the case of a disconnected device,
    // since by then the endpoint would already be in error state.
    zxlogf(ERROR, "xhci_stop_endpoint: TRB_CMD_STOP_ENDPOINT failed cc: %d", cc);
    return ZX_ERR_INTERNAL;
  }

  return xhci_free_endpoint_state(xhci, slot, ep, complete_status);
}

// Returns the slot for the given |hub_address| and |port|, or nullptr if no such slot exists.
// The slot id will be stored in |out_slot_id|.
static xhci_slot_t* xhci_get_slot(xhci_t* xhci, uint32_t hub_address, uint32_t port,
                                  uint32_t* out_slot_id) {
  xhci_slot_t* slot = nullptr;
  uint32_t slot_id;

  int rh_index = xhci_get_root_hub_index(xhci, hub_address);
  if (rh_index >= 0) {
    // For virtual root hub devices, real hub_address is 0
    hub_address = 0;
    // convert virtual root hub port number to real port number
    port = xhci->root_hubs[rh_index].port_map[port - 1] + 1;
  }

  for (slot_id = 1; slot_id <= xhci->max_slots; slot_id++) {
    xhci_slot_t* test_slot = &xhci->slots[slot_id];
    if (test_slot->hub_address == hub_address && test_slot->port == port) {
      slot = test_slot;
      break;
    }
  }
  if (slot) {
    *out_slot_id = slot_id;
  }
  return slot;
}

static zx_status_t xhci_handle_disconnect_device(xhci_t* xhci, uint32_t hub_address,
                                                 uint32_t port) {
  zxlogf(TRACE, "xhci_handle_disconnect_device");
  uint32_t slot_id;
  xhci_slot_t* slot = xhci_get_slot(xhci, hub_address, port, &slot_id);
  if (!slot) {
    zxlogf(ERROR, "slot not found in xhci_handle_disconnect_device");
    return ZX_ERR_NOT_FOUND;
  }

  uint32_t drop_flags = 0;
  for (int i = 0; i < XHCI_NUM_EPS; i++) {
    if (slot->eps[i].state != EP_STATE_DEAD && slot->eps[i].state != EP_STATE_DISABLED) {
      zx_status_t status =
          xhci_stop_endpoint(xhci, slot_id, i, EP_STATE_DEAD, ZX_ERR_IO_NOT_PRESENT);
      if (status != ZX_OK) {
        zxlogf(ERROR, "xhci_handle_disconnect_device: xhci_stop_endpoint failed: %d", status);
      }
      drop_flags |= XHCI_ICC_EP_FLAG(i);
    }
  }

  xhci_remove_device(xhci, slot_id);

  {
    fbl::AutoLock al(&xhci->input_context_lock);
    auto* icc = reinterpret_cast<xhci_input_control_context_t*>(xhci->input_context);
    zx_paddr_t icc_phys = xhci->input_context_phys;
    memset(icc, 0, xhci->context_size);
    XHCI_WRITE32(&icc->drop_context_flags, drop_flags);

    zx_status_t status =
        xhci_send_command(xhci, TRB_CMD_CONFIGURE_EP, icc_phys, (slot_id << TRB_SLOT_ID_START));
    if (status != ZX_OK) {
      zxlogf(ERROR, "xhci_handle_disconnect_device: TRB_CMD_CONFIGURE_EP failed");
    }
  }

  xhci_disable_slot(xhci, slot_id);

  return ZX_OK;
}

static zx_status_t xhci_handle_reset_device(xhci_t* xhci, uint32_t hub_address, uint32_t port) {
  zxlogf(TRACE, "xhci_handle_reset_device %u %u", hub_address, port);
  zx_status_t result;
  uint32_t slot_id;
  xhci_slot_t* slot = xhci_get_slot(xhci, hub_address, port, &slot_id);
  if (!slot) {
    zxlogf(ERROR, "slot not found in xhci_handle_reset_device");
    result = ZX_ERR_NOT_FOUND;
    goto done;
  }
  result = xhci_send_command(xhci, TRB_CMD_RESET_DEVICE, 0, (slot_id << TRB_SLOT_ID_START));
  if (result != ZX_OK) {
    zxlogf(ERROR, "xhci_handle_reset_device: TRB_CMD_RESET_DEVICE failed");
    goto done;
  }

  // TRB_CMD_RESET_DEVICE disables all endpoints except the control endpoint.
  for (int i = 1; i < XHCI_NUM_EPS; i++) {
    xhci_endpoint_t* ep = &slot->eps[i];

    {
      fbl::AutoLock al(&ep->lock);
      ep->state = EP_STATE_DISABLED;
    }

    result = xhci_free_endpoint_state(xhci, slot, ep, ZX_ERR_IO_NOT_PRESENT);
    if (result != ZX_OK) {
      zxlogf(ERROR, "xhci_free_endpoint_state failed slot %u ep %u, err: %d", result, slot_id, i);
    }
  }
  // The slot is now in the Default state and we need to address it again.
  result = xhci_setup_slot(xhci, slot_id, hub_address, port, slot->speed);
  if (result != ZX_OK) {
    zxlogf(ERROR, "xhci_handle_reset_device: xhci_setup_slot failed: %d", result);
    goto done;
  }
  zxlogf(TRACE, "xhci_handle_reset_device %u %u successful", hub_address, port);

done:
  // We should always call this so we can update the device state.
  usb_bus_interface_reinitialize_device(&xhci->bus, slot_id);
  return result;
}

static int xhci_device_thread(void* arg) {
  auto* xhci = static_cast<xhci_t*>(arg);

  while (1) {
    zxlogf(TRACE, "xhci_device_thread top of loop");
    // wait for a device to enumerate
    sync_completion_wait(&xhci->command_queue_completion, ZX_TIME_INFINITE);
    xhci_device_command_t* command;

    {
      fbl::AutoLock al(&xhci->command_queue_mutex);
      list_node_t* node = list_remove_head(&xhci->command_queue);
      command = (node ? containerof(node, xhci_device_command_t, node) : nullptr);
      if (list_is_empty(&xhci->command_queue)) {
        sync_completion_reset(&xhci->command_queue_completion);
      }
    }

    if (!command) {
      zxlogf(ERROR,
             "xhci_device_thread: command_queue_completion was signaled, "
             "but no command was found");
      break;
    }

    switch (command->command) {
      case ENUMERATE_DEVICE:
        xhci_handle_enumerate_device(xhci, command->hub_address, command->port, command->speed);
        break;
      case DISCONNECT_DEVICE:
        xhci_handle_disconnect_device(xhci, command->hub_address, command->port);
        break;
      case RESET_DEVICE:
        xhci_handle_reset_device(xhci, command->hub_address, command->port);
        break;
      case START_ROOT_HUBS:
        xhci_start_root_hubs(xhci);
        break;
      case STOP_THREAD:
        return 0;
    }
    delete command;
  }

  return 0;
}

static zx_status_t xhci_queue_command(xhci_t* xhci, xhci_command_t command, uint32_t hub_address,
                                      uint32_t port, usb_speed_t speed) {
  fbl::AllocChecker ac;
  auto* device_command = new (&ac) xhci_device_command_t;
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  device_command->command = command;
  device_command->hub_address = hub_address;
  device_command->port = port;
  device_command->speed = speed;

  fbl::AutoLock al(&xhci->command_queue_mutex);
  list_add_tail(&xhci->command_queue, &device_command->node);
  sync_completion_signal(&xhci->command_queue_completion);

  return ZX_OK;
}

void xhci_start_device_thread(xhci_t* xhci) {
  thrd_create_with_name(&xhci->device_thread, xhci_device_thread, xhci, "xhci_device_thread");
}

void xhci_stop_device_thread(xhci_t* xhci) {
  xhci_queue_command(xhci, STOP_THREAD, 0, 0, 0);
  thrd_join(xhci->device_thread, nullptr);
}

zx_status_t xhci_enumerate_device(xhci_t* xhci, uint32_t hub_address, uint32_t port,
                                  usb_speed_t speed) {
  return xhci_queue_command(xhci, ENUMERATE_DEVICE, hub_address, port, speed);
}

zx_status_t xhci_device_disconnected(xhci_t* xhci, uint32_t hub_address, uint32_t port) {
  zxlogf(TRACE, "xhci_device_disconnected %d %d", hub_address, port);
  {
    fbl::AutoLock al(&xhci->command_queue_mutex);
    // check pending device list first
    xhci_device_command_t* command;
    list_for_every_entry (&xhci->command_queue, command, xhci_device_command_t, node) {
      if (command->command == ENUMERATE_DEVICE && command->hub_address == hub_address &&
          command->port == port) {
        zxlogf(TRACE, "found on pending list");
        list_delete(&command->node);
        delete command;
        return ZX_OK;
      }
    }
  }

  return xhci_queue_command(xhci, DISCONNECT_DEVICE, hub_address, port, USB_SPEED_UNDEFINED);
}

zx_status_t xhci_device_reset(xhci_t* xhci, uint32_t hub_address, uint32_t port) {
  return xhci_queue_command(xhci, RESET_DEVICE, hub_address, port, USB_SPEED_UNDEFINED);
}

zx_status_t xhci_queue_start_root_hubs(xhci_t* xhci) {
  return xhci_queue_command(xhci, START_ROOT_HUBS, 0, 0, USB_SPEED_UNDEFINED);
}

static zx_status_t xhci_update_input_context(xhci_t* xhci, uint32_t slot_id, int ep_index) {
  zx_paddr_t icc_phys = xhci->input_context_phys;

  return xhci_send_command(xhci, TRB_CMD_CONFIGURE_EP, icc_phys, (slot_id << TRB_SLOT_ID_START));
}

zx_status_t xhci_enable_endpoint(xhci_t* xhci, uint32_t slot_id,
                                 const usb_endpoint_descriptor_t* ep_desc,
                                 const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  if (xhci_is_root_hub(xhci, slot_id)) {
    // nothing to do for root hubs
    return ZX_OK;
  }

  xhci_slot_t* slot = &xhci->slots[slot_id];
  usb_speed_t speed = slot->speed;
  uint32_t index = xhci_endpoint_index(ep_desc->bEndpointAddress);
  xhci_endpoint_t* ep = &slot->eps[index];
  ep->ep_type = usb_ep_type(ep_desc);
  ep->max_packet_size = usb_ep_max_packet(ep_desc);

  fbl::AutoLock al(&ep->lock);

  if (ep->state == EP_STATE_RUNNING) {
    return ZX_OK;
  }

  fbl::AutoLock al2(&xhci->input_context_lock);

  auto* icc = reinterpret_cast<xhci_input_control_context_t*>(xhci->input_context);
  auto* sc = reinterpret_cast<xhci_slot_context_t*>(&xhci->input_context[1 * xhci->context_size]);
  memset(icc, 0, xhci->context_size);

  memset(sc, 0, xhci->context_size);

  uint32_t ep_type = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
  uint32_t ep_index = ep_type;
  if ((ep_desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN) {
    ep_index += 4;
  }

  // See Table 65 in XHCI spec
  int cerr = (ep_type == USB_ENDPOINT_ISOCHRONOUS ? 0 : 3);
  int max_packet_size = usb_ep_max_packet(ep_desc);

  int max_burst = 0;
  if (speed == USB_SPEED_SUPER) {
    if (ss_comp_desc != nullptr) {
      max_burst = ss_comp_desc->bMaxBurst;
    }
  } else if (speed == USB_SPEED_HIGH) {
    if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
      max_burst = usb_ep_add_mf_transactions(ep_desc);
    }
  }

  int avg_trb_length = max_packet_size * max_burst;
  int max_esit_payload = 0;
  if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
    // FIXME - more work needed for superspeed here
    max_esit_payload = max_packet_size * max_burst;
  }

  auto* epc = reinterpret_cast<xhci_endpoint_context_t*>(
      &xhci->input_context[(index + 2) * xhci->context_size]);
  memset(epc, 0, xhci->context_size);
  // allocate a transfer ring for the endpoint
  zx_status_t status =
      xhci_transfer_ring_init(&ep->transfer_ring, xhci->bti_handle.get(), TRANSFER_RING_SIZE);
  if (status < 0) {
    return status;
  }

  zx_paddr_t tr_dequeue = slot->eps[index].transfer_ring.buffers.front()->phys_list()[0];

  XHCI_SET_BITS32(&epc->epc0, EP_CTX_INTERVAL_START, EP_CTX_INTERVAL_BITS,
                  compute_interval(ep_desc, speed));
  XHCI_SET_BITS32(&epc->epc0, EP_CTX_MAX_ESIT_PAYLOAD_HI_START, EP_CTX_MAX_ESIT_PAYLOAD_HI_BITS,
                  max_esit_payload >> EP_CTX_MAX_ESIT_PAYLOAD_LO_BITS);
  XHCI_SET_BITS32(&epc->epc1, EP_CTX_CERR_START, EP_CTX_CERR_BITS, cerr);
  XHCI_SET_BITS32(&epc->epc1, EP_CTX_EP_TYPE_START, EP_CTX_EP_TYPE_BITS, ep_index);
  XHCI_SET_BITS32(&epc->epc1, EP_CTX_MAX_PACKET_SIZE_START, EP_CTX_MAX_PACKET_SIZE_BITS,
                  max_packet_size);
  XHCI_SET_BITS32(&epc->epc1, EP_CTX_MAX_BURST_SIZE_START, EP_CTX_MAX_BURST_SIZE_BITS, max_burst);

  XHCI_WRITE32(&epc->epc2, ((uint32_t)tr_dequeue & EP_CTX_TR_DEQUEUE_LO_MASK) | EP_CTX_DCS);
  XHCI_WRITE32(&epc->tr_dequeue_hi, (uint32_t)(tr_dequeue >> 32));
  XHCI_SET_BITS32(&epc->epc4, EP_CTX_AVG_TRB_LENGTH_START, EP_CTX_AVG_TRB_LENGTH_BITS,
                  avg_trb_length);
  XHCI_SET_BITS32(&epc->epc4, EP_CTX_MAX_ESIT_PAYLOAD_LO_START, EP_CTX_MAX_ESIT_PAYLOAD_LO_BITS,
                  max_esit_payload);

  XHCI_WRITE32(&icc->add_context_flags, XHCI_ICC_SLOT_FLAG | XHCI_ICC_EP_FLAG(index));

  XHCI_WRITE32(&sc->sc0, XHCI_READ32(&slot->sc->sc0));
  XHCI_WRITE32(&sc->sc1, XHCI_READ32(&slot->sc->sc1));
  XHCI_WRITE32(&sc->sc2, XHCI_READ32(&slot->sc->sc2));
  XHCI_SET_BITS32(&sc->sc0, SLOT_CTX_CONTEXT_ENTRIES_START, SLOT_CTX_CONTEXT_ENTRIES_BITS,
                  index + 1);

  status = xhci_update_input_context(xhci, slot_id, index);

  // xhci_stop_endpoint() will handle the !enable case
  if (status == ZX_OK) {
    fbl::AllocChecker ac;
    ep->transfer_state = new (&ac) xhci_transfer_state_t;
    if (!ac.check()) {
      status = ZX_ERR_NO_MEMORY;
    } else {
      memset(ep->transfer_state, 0, sizeof(*ep->transfer_state));
      ep->state = EP_STATE_RUNNING;
    }
  }
  return status;
}

zx_status_t xhci_disable_endpoint(xhci_t* xhci, uint32_t slot_id,
                                  const usb_endpoint_descriptor_t* ep_desc) {
  if (xhci_is_root_hub(xhci, slot_id)) {
    // nothing to do for root hubs
    return ZX_OK;
  }

  xhci_slot_t* slot = &xhci->slots[slot_id];
  uint32_t index = xhci_endpoint_index(ep_desc->bEndpointAddress);
  xhci_endpoint_t* ep = &slot->eps[index];
  ep->ep_type = usb_ep_type(ep_desc);
  ep->max_packet_size = usb_ep_max_packet(ep_desc);

  {
    fbl::AutoLock al(&ep->lock);

    if (ep->state == EP_STATE_DISABLED) {
      return ZX_OK;
    }
  }

  fbl::AutoLock al(&xhci->input_context_lock);

  auto* icc = reinterpret_cast<xhci_input_control_context_t*>(xhci->input_context);
  memset(icc, 0, xhci->context_size);

  // xhci_stop_endpoint will try to acquire the endpoint lock.
  // It also needs to wait for the TRB_CMD_STOP_ENDPOINT completion, which may never
  // complete if another xhci event is waiting for the same endpoint lock.
  xhci_stop_endpoint(xhci, slot_id, index, EP_STATE_DISABLED, ZX_ERR_BAD_STATE);
  XHCI_WRITE32(&icc->drop_context_flags, XHCI_ICC_EP_FLAG(index));
  return xhci_update_input_context(xhci, slot_id, index);
}

zx_status_t xhci_configure_hub(xhci_t* xhci, uint32_t slot_id, usb_speed_t speed,
                               const usb_hub_descriptor_t* descriptor) {
  zxlogf(TRACE, "xhci_configure_hub slot_id: %d speed: %d", slot_id, speed);
  if (xhci_is_root_hub(xhci, slot_id)) {
    // nothing to do for root hubs
    return ZX_OK;
  }
  if (slot_id > xhci->max_slots)
    return ZX_ERR_INVALID_ARGS;

  xhci_slot_t* slot = &xhci->slots[slot_id];
  uint32_t num_ports = descriptor->bNbrPorts;
  uint32_t ttt = 0;
  if (speed == USB_SPEED_HIGH) {
    ttt = (descriptor->wHubCharacteristics >> 5) & 3;
  }
  // TODO: Check for MTT. Needs a hook for calling set_interface from usb layer.
  {
    fbl::AutoLock al(&xhci->input_context_lock);
    auto* icc = reinterpret_cast<xhci_input_control_context_t*>(xhci->input_context);
    zx_paddr_t icc_phys = xhci->input_context_phys;
    auto* sc = reinterpret_cast<xhci_slot_context_t*>(&xhci->input_context[xhci->context_size]);
    memset(icc, 0, xhci->context_size);
    memset(sc, 0, xhci->context_size);

    XHCI_WRITE32(&icc->add_context_flags, XHCI_ICC_SLOT_FLAG);
    XHCI_WRITE32(&sc->sc0, XHCI_READ32(&slot->sc->sc0) | SLOT_CTX_HUB);
    XHCI_WRITE32(&sc->sc1, XHCI_READ32(&slot->sc->sc1));
    XHCI_WRITE32(&sc->sc2, XHCI_READ32(&slot->sc->sc2));

    XHCI_SET_BITS32(&sc->sc1, SLOT_CTX_ROOT_NUM_PORTS_START, SLOT_CTX_ROOT_NUM_PORTS_BITS,
                    num_ports);
    XHCI_SET_BITS32(&sc->sc2, SLOT_CTX_TTT_START, SLOT_CTX_TTT_BITS, ttt);

    zx_status_t status =
        xhci_send_command(xhci, TRB_CMD_EVAL_CONTEXT, icc_phys, (slot_id << TRB_SLOT_ID_START));
    if (status != ZX_OK) {
      zxlogf(ERROR, "xhci_configure_hub: TRB_CMD_EVAL_CONTEXT failed");
      return status;
    }
  }

  if (speed == USB_SPEED_SUPER) {
    // compute hub depth
    uint16_t depth = 0;
    while (slot->hub_address != 0) {
      depth++;
      slot = &xhci->slots[slot->hub_address];
    }

    zxlogf(TRACE, "USB_HUB_SET_DEPTH %d", depth);
    zx_status_t result =
        xhci_control_request(xhci, slot_id, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE,
                             USB_HUB_SET_DEPTH, depth, 0, nullptr, 0, nullptr);
    if (result < 0) {
      zxlogf(ERROR, "xhci_configure_hub: USB_HUB_SET_DEPTH failed");
    }
  }

  return ZX_OK;
}

}  // namespace usb_xhci
