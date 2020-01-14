// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb/composite.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#define CDC_SUPPORTED_VERSION 0x0110 /* 1.10 */

// The maximum amount of memory we are willing to allocate to transaction buffers
#define MAX_TX_BUF_SZ 32768
#define MAX_RX_BUF_SZ 1500 * 2048

#define ETHERNET_MAX_TRANSMIT_DELAY 100
#define ETHERNET_MAX_RECV_DELAY 100
#define ETHERNET_TRANSMIT_DELAY 10
#define ETHERNET_RECV_DELAY 10
#define ETHERNET_INITIAL_TRANSMIT_DELAY 0
#define ETHERNET_INITIAL_RECV_DELAY 0
#define ETHERNET_INITIAL_PACKET_FILTER \
  (USB_CDC_PACKET_TYPE_DIRECTED | USB_CDC_PACKET_TYPE_BROADCAST | USB_CDC_PACKET_TYPE_MULTICAST)

const char* module_name = "usb-cdc-ecm";

typedef struct {
  uint8_t addr;
  uint16_t max_packet_size;
} ecm_endpoint_t;

typedef struct {
  zx_device_t* zxdev;
  zx_device_t* usb_device;
  usb_protocol_t usb;
  // Ethernet lock -- must be acquired after tx_mutex
  // when both locks are held.
  mtx_t ethernet_mutex;
  ethernet_ifc_protocol_t ethernet_ifc;

  // Device attributes
  uint8_t mac_addr[ETH_MAC_SIZE];
  uint16_t mtu;

  // Connection attributes
  bool online;
  uint32_t ds_bps;
  uint32_t us_bps;

  // Interrupt handling
  ecm_endpoint_t int_endpoint;
  usb_request_t* int_txn_buf;
  sync_completion_t completion;
  thrd_t int_thread;

  // Send context
  // TX lock -- Must be acquired before ethernet_mutex
  // when both locks are held.
  mtx_t tx_mutex;
  ecm_endpoint_t tx_endpoint;
  list_node_t tx_txn_bufs;       // list of usb_request_t
  list_node_t tx_pending_infos;  // list of txn_info_t
  bool unbound;                  // set to true when device is going away. Guarded by tx_mutex
  uint64_t tx_endpoint_delay;    // wait time between 2 transmit requests

  size_t parent_req_size;

  // Receive context
  ecm_endpoint_t rx_endpoint;
  uint64_t rx_endpoint_delay;  // wait time between 2 recv requests
  uint16_t rx_packet_filter;
} ecm_ctx_t;

typedef struct txn_info {
  ethernet_netbuf_t netbuf;
  ethernet_impl_queue_tx_callback completion_cb;
  void* cookie;
  list_node_t node;
} txn_info_t;

static void complete_txn(txn_info_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->netbuf);
}

static void usb_write_complete(void* cookie, usb_request_t* request);

static void ecm_unbind(void* cookie) {
  zxlogf(TRACE, "%s: unbinding\n", module_name);
  ecm_ctx_t* ctx = cookie;

  mtx_lock(&ctx->tx_mutex);
  ctx->unbound = true;
  txn_info_t* txn;
  while ((txn = list_remove_head_type(&ctx->tx_pending_infos, txn_info_t, node)) != NULL) {
    complete_txn(txn, ZX_ERR_PEER_CLOSED);
  }
  mtx_unlock(&ctx->tx_mutex);

  device_unbind_reply(ctx->zxdev);
}

static void ecm_free(ecm_ctx_t* ctx) {
  zxlogf(TRACE, "%s: deallocating memory\n", module_name);
  if (ctx->int_thread) {
    thrd_join(ctx->int_thread, NULL);
  }
  usb_request_t* txn;
  while ((txn = usb_req_list_remove_head(&ctx->tx_txn_bufs, ctx->parent_req_size)) != NULL) {
    usb_request_release(txn);
  }
  if (ctx->int_txn_buf) {
    usb_request_release(ctx->int_txn_buf);
  }
  mtx_destroy(&ctx->ethernet_mutex);
  mtx_destroy(&ctx->tx_mutex);
  free(ctx);
}

static void ecm_release(void* ctx) {
  ecm_ctx_t* eth = ctx;
  ecm_free(eth);
}

static zx_protocol_device_t ecm_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ecm_unbind,
    .release = ecm_release,
};

static void ecm_update_online_status(ecm_ctx_t* ctx, bool is_online) {
  mtx_lock(&ctx->ethernet_mutex);
  if ((is_online && ctx->online) || (!is_online && !ctx->online)) {
    goto done;
  }

  if (is_online) {
    zxlogf(INFO, "%s: connected to network\n", module_name);
    ctx->online = true;
    if (ctx->ethernet_ifc.ops) {
      ethernet_ifc_status(&ctx->ethernet_ifc, ETHERNET_STATUS_ONLINE);
    } else {
      zxlogf(ERROR, "%s: not connected to ethermac interface\n", module_name);
    }
  } else {
    zxlogf(INFO, "%s: no connection to network\n", module_name);
    ctx->online = false;
    if (ctx->ethernet_ifc.ops) {
      ethernet_ifc_status(&ctx->ethernet_ifc, 0);
    }
  }

done:
  mtx_unlock(&ctx->ethernet_mutex);
}

static zx_status_t ecm_ethernet_impl_query(void* ctx, uint32_t options, ethernet_info_t* info) {
  ecm_ctx_t* eth = ctx;

  zxlogf(TRACE, "%s: %s called\n", module_name, __FUNCTION__);

  // No options are supported
  if (options) {
    zxlogf(ERROR, "%s: unexpected options (0x%" PRIx32 ") to ecm_ethernet_impl_query\n",
           module_name, options);
    return ZX_ERR_INVALID_ARGS;
  }

  memset(info, 0, sizeof(*info));
  info->mtu = eth->mtu;
  memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));
  info->netbuf_size = sizeof(txn_info_t);

  return ZX_OK;
}

static void ecm_ethernet_impl_stop(void* cookie) {
  zxlogf(TRACE, "%s: %s called\n", module_name, __FUNCTION__);
  ecm_ctx_t* ctx = cookie;
  mtx_lock(&ctx->tx_mutex);
  mtx_lock(&ctx->ethernet_mutex);
  ctx->ethernet_ifc.ops = NULL;
  mtx_unlock(&ctx->ethernet_mutex);
  mtx_unlock(&ctx->tx_mutex);
}

static zx_status_t ecm_ethernet_impl_start(void* ctx_cookie, const ethernet_ifc_protocol_t* ifc) {
  zxlogf(TRACE, "%s: %s called\n", module_name, __FUNCTION__);
  ecm_ctx_t* ctx = ctx_cookie;
  zx_status_t status = ZX_OK;

  mtx_lock(&ctx->ethernet_mutex);
  if (ctx->ethernet_ifc.ops) {
    status = ZX_ERR_ALREADY_BOUND;
  } else {
    ctx->ethernet_ifc = *ifc;
    ethernet_ifc_status(&ctx->ethernet_ifc, ctx->online ? ETHERNET_STATUS_ONLINE : 0);
  }
  mtx_unlock(&ctx->ethernet_mutex);

  return status;
}

static zx_status_t queue_request(ecm_ctx_t* ctx, const uint8_t* data, size_t length,
                                 usb_request_t* req) {
  req->header.length = length;
  if (!ctx->ethernet_ifc.ops) {
    return ZX_ERR_BAD_STATE;
  }
  ssize_t bytes_copied = usb_request_copy_to(req, data, length, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "%s: failed to copy data into send txn (error %zd)\n", module_name, bytes_copied);
    return ZX_ERR_IO;
  }
  usb_request_complete_t complete = {
      .callback = usb_write_complete,
      .ctx = ctx,
  };
  usb_request_queue(&ctx->usb, req, &complete);
  return ZX_OK;
}

static zx_status_t send_locked(ecm_ctx_t* ctx, ethernet_netbuf_t* netbuf) {
  const uint8_t* byte_data = netbuf->data_buffer;
  size_t length = netbuf->data_size;

  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* tx_req = usb_req_list_remove_head(&ctx->tx_txn_bufs, ctx->parent_req_size);
  if (tx_req == NULL) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(ctx->tx_endpoint_delay)));
  zx_status_t status;
  if ((status = queue_request(ctx, byte_data, length, tx_req)) != ZX_OK) {
    zx_status_t add_status = usb_req_list_add_tail(&ctx->tx_txn_bufs, tx_req, ctx->parent_req_size);
    ZX_DEBUG_ASSERT(add_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

// Write completion callback. Normally -- this will simply acquire the TX lock, release it,
// and re-queue the USB request.
// The error case is a bit more complicated. We set the reset bit on the request, and queue
// a packet that triggers a reset (asynchronously). We then immediately return to the interrupt
// thread with the lock held to allow for interrupt processing to take place. Once the reset
// completes, this function is called again with the lock still held, and request processing
// continues normally. It is necessary to keep the lock held after returning in the error case
// because we do not want other packets to get queued out-of-order while the asynchronous operation
// is in progress.
static void usb_write_complete(void* cookie,
                               usb_request_t* request) __TA_NO_THREAD_SAFETY_ANALYSIS {
  ecm_ctx_t* ctx = cookie;

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }
  // If reset, we still hold the TX mutex.
  if (!request->reset) {
    mtx_lock(&ctx->tx_mutex);
    // Return transmission buffer to pool
    zx_status_t status = usb_req_list_add_tail(&ctx->tx_txn_bufs, request, ctx->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    if (request->response.status == ZX_ERR_IO_REFUSED) {
      zxlogf(TRACE, "%s: resetting transmit endpoint\n", module_name);
      request->reset = true;
      request->reset_address = ctx->tx_endpoint.addr;
      usb_request_complete_t complete = {
          .callback = usb_write_complete,
          .ctx = ctx,
      };
      usb_request_queue(&ctx->usb, request, &complete);
      return;
    }

    if (request->response.status == ZX_ERR_IO_INVALID) {
      zxlogf(TRACE,
             "%s: slowing down the requests by %d usec."
             "Resetting the transmit endpoint\n",
             module_name, ETHERNET_TRANSMIT_DELAY);
      if (ctx->tx_endpoint_delay < ETHERNET_MAX_TRANSMIT_DELAY) {
        ctx->tx_endpoint_delay += ETHERNET_TRANSMIT_DELAY;
      }
      request->reset = true;
      request->reset_address = ctx->tx_endpoint.addr;
      usb_request_complete_t complete = {
          .callback = usb_write_complete,
          .ctx = ctx,
      };
      usb_request_queue(&ctx->usb, request, &complete);
      return;
    }
  }
  request->reset = false;

  bool additional_tx_queued = false;
  txn_info_t* txn;
  zx_status_t send_status = ZX_OK;
  if (!list_is_empty(&ctx->tx_pending_infos)) {
    txn = list_peek_head_type(&ctx->tx_pending_infos, txn_info_t, node);
    if ((send_status = send_locked(ctx, &txn->netbuf)) != ZX_ERR_SHOULD_WAIT) {
      list_remove_head(&ctx->tx_pending_infos);
      additional_tx_queued = true;
    }
  }

  mtx_unlock(&ctx->tx_mutex);

  mtx_lock(&ctx->ethernet_mutex);
  if (additional_tx_queued) {
    complete_txn(txn, send_status);
  }
  mtx_unlock(&ctx->ethernet_mutex);

  // When the interface is offline, the transaction will complete with status set to
  // ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

// Note: the assumption made here is that no rx transmissions will be processed in parallel,
// so we do not maintain an rx mutex.
static void usb_recv(ecm_ctx_t* ctx, usb_request_t* request) {
  size_t len = request->response.actual;

  uint8_t* read_data;
  zx_status_t status = usb_request_mmap(request, (void*)&read_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_request_mmap failed with status %d\n", module_name, status);
    return;
  }

  mtx_lock(&ctx->ethernet_mutex);
  if (ctx->ethernet_ifc.ops) {
    ethernet_ifc_recv(&ctx->ethernet_ifc, read_data, len, 0);
  }
  mtx_unlock(&ctx->ethernet_mutex);
}

static void usb_read_complete(void* cookie, usb_request_t* request) __TA_NO_THREAD_SAFETY_ANALYSIS {
  ecm_ctx_t* ctx = cookie;
  if (request->response.status != ZX_OK) {
    zxlogf(TRACE, "%s: usb_read_complete called with status %d\n", module_name,
           (int)request->response.status);
  }

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(TRACE, "%s: resetting receive endpoint\n", module_name);
    request->reset = true;
    request->reset_address = ctx->rx_endpoint.addr;
    usb_request_complete_t complete = {
        .callback = usb_read_complete,
        .ctx = ctx,
    };
    usb_request_queue(&ctx->usb, request, &complete);
    return;
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    if (ctx->rx_endpoint_delay < ETHERNET_MAX_RECV_DELAY) {
      ctx->rx_endpoint_delay += ETHERNET_RECV_DELAY;
    }
    zxlogf(TRACE,
           "%s: slowing down the requests by %d usec."
           "Resetting the recv endpoint\n",
           module_name, ETHERNET_RECV_DELAY);
    request->reset = true;
    request->reset_address = ctx->rx_endpoint.addr;
    usb_request_complete_t complete = {
        .callback = usb_read_complete,
        .ctx = ctx,
    };
    usb_request_queue(&ctx->usb, request, &complete);
    return;
  } else if (request->response.status == ZX_OK && !request->reset) {
    usb_recv(ctx, request);
  }
  if (ctx->rx_endpoint_delay) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(ctx->rx_endpoint_delay)));
  }
  request->reset = false;
  usb_request_complete_t complete = {
      .callback = usb_read_complete,
      .ctx = ctx,
  };
  usb_request_queue(&ctx->usb, request, &complete);
}

static void ecm_ethernet_impl_queue_tx(void* context, uint32_t options, ethernet_netbuf_t* netbuf,
                                       ethernet_impl_queue_tx_callback completion_cb,
                                       void* cookie) {
  ecm_ctx_t* ctx = context;
  size_t length = netbuf->data_size;
  zx_status_t status;

  txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

  if (length > ctx->mtu || length == 0) {
    complete_txn(txn, ZX_ERR_INVALID_ARGS);
    return;
  }

  zxlogf(SPEW, "%s: sending %zu bytes to endpoint 0x%" PRIx8 "\n", module_name, length,
         ctx->tx_endpoint.addr);

  mtx_lock(&ctx->tx_mutex);
  if (ctx->unbound) {
    status = ZX_ERR_IO_NOT_PRESENT;
  } else {
    status = send_locked(ctx, netbuf);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // No buffers available, queue it up
      list_add_tail(&ctx->tx_pending_infos, &txn->node);
    }
  }

  mtx_unlock(&ctx->tx_mutex);
  if (status != ZX_ERR_SHOULD_WAIT) {
    complete_txn(txn, status);
  }
}

static zx_status_t ecm_ethernet_impl_manipulate_bits(ecm_ctx_t* eth, uint16_t mode, bool on) {
  zx_status_t status = ZX_OK;
  uint16_t bits = eth->rx_packet_filter;

  if (on) {
    bits |= mode;
  } else {
    bits &= ~mode;
  }

  status = usb_control_out(&eth->usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_CDC_SET_ETHERNET_PACKET_FILTER, bits, 0, ZX_TIME_INFINITE, NULL, 0);

  if (status != ZX_OK) {
    zxlogf(ERROR, "usb-cdc-ecm: Set packet filter failed: %d\n", status);
    return status;
  }
  eth->rx_packet_filter = bits;
  return status;
}

static zx_status_t ecm_ethernet_impl_set_param(void* cookie, uint32_t param, int32_t value,
                                               const void* data, size_t data_size) {
  zx_status_t status;
  ecm_ctx_t* ctx = cookie;

  mtx_lock(&ctx->tx_mutex);

  switch (param) {
    case ETHERNET_SETPARAM_PROMISC:
      status = ecm_ethernet_impl_manipulate_bits(ctx, USB_CDC_PACKET_TYPE_PROMISCUOUS, (bool)value);
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
  }

  mtx_unlock(&ctx->tx_mutex);

  return status;
}

static ethernet_impl_protocol_ops_t ethernet_impl_ops = {
    .query = ecm_ethernet_impl_query,
    .stop = ecm_ethernet_impl_stop,
    .start = ecm_ethernet_impl_start,
    .queue_tx = ecm_ethernet_impl_queue_tx,
    .set_param = ecm_ethernet_impl_set_param,
};

static void ecm_interrupt_complete(void* cookie, usb_request_t* request) {
  ecm_ctx_t* ctx = cookie;
  sync_completion_signal(&ctx->completion);
}

static void ecm_handle_interrupt(ecm_ctx_t* ctx, usb_request_t* request) {
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(ERROR, "%s: ignored interrupt (size = %ld)\n", module_name,
           (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req;
  usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);
  if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
      usb_req.bNotification == USB_CDC_NC_NETWORK_CONNECTION) {
    ecm_update_online_status(ctx, usb_req.wValue != 0);
  } else if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
             usb_req.bNotification == USB_CDC_NC_CONNECTION_SPEED_CHANGE) {
    // The ethermac driver doesn't care about speed changes, so even though we track this
    // information, it's currently unused.
    if (usb_req.wLength != 8) {
      zxlogf(ERROR, "%s: invalid size (%" PRIu16 ") for CONNECTION_SPEED_CHANGE notification\n",
             module_name, usb_req.wLength);
      return;
    }
    // Data immediately follows notification in packet
    uint32_t new_us_bps, new_ds_bps;
    usb_request_copy_from(request, &new_us_bps, 4, sizeof(usb_cdc_notification_t));
    usb_request_copy_from(request, &new_ds_bps, 4, sizeof(usb_cdc_notification_t) + 4);
    if (new_us_bps != ctx->us_bps) {
      zxlogf(ERROR, "%s: connection speed change... upstream bits/s: %" PRIu32 "\n", module_name,
             new_us_bps);
      ctx->us_bps = new_us_bps;
    }
    if (new_ds_bps != ctx->ds_bps) {
      zxlogf(ERROR, "%s: connection speed change... downstream bits/s: %" PRIu32 "\n", module_name,
             new_ds_bps);
      ctx->ds_bps = new_ds_bps;
    }
  } else {
    zxlogf(ERROR, "%s: ignored interrupt (type = %" PRIu8 ", request = %" PRIu8 ")\n", module_name,
           usb_req.bmRequestType, usb_req.bNotification);
    return;
  }
}

static int ecm_int_handler_thread(void* cookie) {
  ecm_ctx_t* ctx = cookie;
  usb_request_t* txn = ctx->int_txn_buf;

  usb_request_complete_t complete = {
      .callback = ecm_interrupt_complete,
      .ctx = ctx,
  };
  while (true) {
    sync_completion_reset(&ctx->completion);
    usb_request_queue(&ctx->usb, txn, &complete);
    sync_completion_wait(&ctx->completion, ZX_TIME_INFINITE);
    if (txn->response.status == ZX_OK) {
      ecm_handle_interrupt(ctx, txn);
    } else if (txn->response.status == ZX_ERR_PEER_CLOSED ||
               txn->response.status == ZX_ERR_IO_NOT_PRESENT) {
      zxlogf(TRACE, "%s: terminating interrupt handling thread\n", module_name);
      return txn->response.status;
    } else if (txn->response.status == ZX_ERR_IO_REFUSED ||
               txn->response.status == ZX_ERR_IO_INVALID) {
      zxlogf(TRACE, "%s: resetting interrupt endpoint\n", module_name);
      usb_reset_endpoint(&ctx->usb, ctx->int_endpoint.addr);
    } else {
      zxlogf(ERROR, "%s: error (%ld) waiting for interrupt - ignoring\n", module_name,
             (long)txn->response.status);
    }
  }
}

static bool parse_cdc_header(usb_cs_header_interface_descriptor_t* header_desc) {
  // Check for supported CDC version
  zxlogf(TRACE, "%s: device reports CDC version as 0x%x\n", module_name, header_desc->bcdCDC);
  return header_desc->bcdCDC >= CDC_SUPPORTED_VERSION;
}

static bool parse_cdc_ethernet_descriptor(ecm_ctx_t* ctx,
                                          usb_cs_ethernet_interface_descriptor_t* desc) {
  ctx->mtu = desc->wMaxSegmentSize;

  // MAC address is stored in a string descriptor in UTF-16 format, so we get one byte of
  // address for each 32 bits of text.
  const size_t expected_str_size = sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * 4;
  char str_desc_buf[expected_str_size];

  // Read string descriptor for MAC address (string index is in iMACAddress field)
  size_t out_length;
  zx_status_t result =
      usb_get_descriptor(&ctx->usb, 0, USB_DT_STRING, desc->iMACAddress, str_desc_buf,
                         sizeof(str_desc_buf), ZX_TIME_INFINITE, &out_length);
  if (result < 0) {
    zxlogf(ERROR, "%s: error reading MAC address\n", module_name);
    return false;
  }
  if (out_length != expected_str_size) {
    zxlogf(ERROR, "%s: MAC address string incorrect length (saw %zd, expected %zd)\n", module_name,
           out_length, expected_str_size);
    return false;
  }

  // Convert MAC address to something more machine-friendly
  usb_string_descriptor_t* str_desc = (usb_string_descriptor_t*)str_desc_buf;
  uint8_t* str = str_desc->bString;
  size_t ndx;
  for (ndx = 0; ndx < ETH_MAC_SIZE * 4; ndx++) {
    if (ndx % 2 == 1) {
      if (str[ndx] != 0) {
        zxlogf(ERROR, "%s: MAC address contains invalid characters\n", module_name);
        return false;
      }
      continue;
    }
    uint8_t value;
    if (str[ndx] >= '0' && str[ndx] <= '9') {
      value = str[ndx] - '0';
    } else if (str[ndx] >= 'A' && str[ndx] <= 'F') {
      value = (str[ndx] - 'A') + 0xa;
    } else {
      zxlogf(ERROR, "%s: MAC address contains invalid characters\n", module_name);
      return false;
    }
    if (ndx % 4 == 0) {
      ctx->mac_addr[ndx / 4] = value << 4;
    } else {
      ctx->mac_addr[ndx / 4] |= value;
    }
  }

  zxlogf(ERROR, "%s: MAC address is %02X:%02X:%02X:%02X:%02X:%02X\n", module_name, ctx->mac_addr[0],
         ctx->mac_addr[1], ctx->mac_addr[2], ctx->mac_addr[3], ctx->mac_addr[4], ctx->mac_addr[5]);
  return true;
}

static void copy_endpoint_info(ecm_endpoint_t* ep_info, usb_endpoint_descriptor_t* desc) {
  ep_info->addr = desc->bEndpointAddress;
  ep_info->max_packet_size = desc->wMaxPacketSize;
}

static bool want_interface(usb_interface_descriptor_t* intf, void* arg) {
  return intf->bInterfaceClass == USB_CLASS_CDC;
}

static zx_status_t ecm_bind(void* ctx, zx_device_t* device) {
  zxlogf(TRACE, "%s: starting %s\n", module_name, __FUNCTION__);

  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
    return result;
  }
  usb_composite_protocol_t usb_composite;
  result = device_get_protocol(device, ZX_PROTOCOL_USB_COMPOSITE, &usb_composite);
  if (result != ZX_OK) {
    return result;
  }

  // Allocate context
  ecm_ctx_t* ecm_ctx = calloc(1, sizeof(ecm_ctx_t));
  if (!ecm_ctx) {
    zxlogf(ERROR, "%s: failed to allocate memory for USB CDC ECM driver\n", module_name);
    return ZX_ERR_NO_MEMORY;
  }

  result = usb_claim_additional_interfaces(&usb_composite, want_interface, NULL);
  if (result != ZX_OK) {
    goto fail;
  }
  // Initialize context
  ecm_ctx->usb_device = device;
  memcpy(&ecm_ctx->usb, &usb, sizeof(ecm_ctx->usb));
  list_initialize(&ecm_ctx->tx_txn_bufs);
  list_initialize(&ecm_ctx->tx_pending_infos);
  mtx_init(&ecm_ctx->ethernet_mutex, mtx_plain);
  mtx_init(&ecm_ctx->tx_mutex, mtx_plain);
  result = usb_control_out(&ecm_ctx->usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_CDC_SET_ETHERNET_PACKET_FILTER, ETHERNET_INITIAL_PACKET_FILTER, 0,
                           ZX_TIME_INFINITE, NULL, 0);
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: failed to set initial packet filter: %d\n", module_name, (int)result);
    goto fail;
  }
  ecm_ctx->rx_packet_filter = ETHERNET_INITIAL_PACKET_FILTER;
  ecm_ctx->parent_req_size = usb_get_request_size(&ecm_ctx->usb);

  usb_desc_iter_t iter;
  result = usb_desc_iter_init(&usb, &iter);
  if (result != ZX_OK) {
    goto fail;
  }
  result = ZX_ERR_NOT_SUPPORTED;

  // Find the CDC descriptors and endpoints
  usb_descriptor_header_t* desc;
  usb_cs_header_interface_descriptor_t* cdc_header_desc = NULL;
  usb_cs_ethernet_interface_descriptor_t* cdc_eth_desc = NULL;
  usb_endpoint_descriptor_t* int_ep = NULL;
  usb_endpoint_descriptor_t* tx_ep = NULL;
  usb_endpoint_descriptor_t* rx_ep = NULL;
  usb_interface_descriptor_t* default_ifc = NULL;
  usb_interface_descriptor_t* data_ifc = NULL;
  while ((desc = usb_desc_iter_peek(&iter)) != NULL) {
    if (desc->bDescriptorType == USB_DT_INTERFACE) {
      usb_interface_descriptor_t* ifc_desc =
          usb_desc_iter_get_structure(&iter, sizeof(usb_interface_descriptor_t));
      if (ifc_desc == NULL) {
        goto fail;
      }
      if (ifc_desc->bInterfaceClass == USB_CLASS_CDC) {
        if (ifc_desc->bNumEndpoints == 0) {
          if (default_ifc) {
            zxlogf(ERROR, "%s: multiple default interfaces found\n", module_name);
            goto fail;
          }
          default_ifc = ifc_desc;
        } else if (ifc_desc->bNumEndpoints == 2) {
          if (data_ifc) {
            zxlogf(ERROR, "%s: multiple data interfaces found\n", module_name);
            goto fail;
          }
          data_ifc = ifc_desc;
        }
      }
    } else if (desc->bDescriptorType == USB_DT_CS_INTERFACE) {
      usb_cs_interface_descriptor_t* cs_ifc_desc =
          usb_desc_iter_get_structure(&iter, sizeof(usb_cs_interface_descriptor_t));
      if (cs_ifc_desc == NULL) {
        goto fail;
      }
      if (cs_ifc_desc->bDescriptorSubType == USB_CDC_DST_HEADER) {
        if (cdc_header_desc != NULL) {
          zxlogf(ERROR, "%s: multiple CDC headers\n", module_name);
          goto fail;
        }
        cdc_header_desc =
            usb_desc_iter_get_structure(&iter, sizeof(usb_cs_header_interface_descriptor_t));
      } else if (cs_ifc_desc->bDescriptorSubType == USB_CDC_DST_ETHERNET) {
        if (cdc_eth_desc != NULL) {
          zxlogf(ERROR, "%s: multiple CDC ethernet descriptors\n", module_name);
          goto fail;
        }
        cdc_eth_desc =
            usb_desc_iter_get_structure(&iter, sizeof(usb_cs_ethernet_interface_descriptor_t));
      }
    } else if (desc->bDescriptorType == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endpoint_desc =
          usb_desc_iter_get_structure(&iter, sizeof(usb_endpoint_descriptor_t));
      if (endpoint_desc == NULL) {
        goto fail;
      }
      if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
          usb_ep_type(endpoint_desc) == USB_ENDPOINT_INTERRUPT) {
        if (int_ep != NULL) {
          zxlogf(ERROR, "%s: multiple interrupt endpoint descriptors\n", module_name);
          goto fail;
        }
        int_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_OUT &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (tx_ep != NULL) {
          zxlogf(ERROR, "%s: multiple tx endpoint descriptors\n", module_name);
          goto fail;
        }
        tx_ep = endpoint_desc;
      } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                 usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
        if (rx_ep != NULL) {
          zxlogf(ERROR, "%s: multiple rx endpoint descriptors\n", module_name);
          goto fail;
        }
        rx_ep = endpoint_desc;
      } else {
        zxlogf(ERROR, "%s: unrecognized endpoint\n", module_name);
        goto fail;
      }
    }
    usb_desc_iter_advance(&iter);
  }
  if (cdc_header_desc == NULL || cdc_eth_desc == NULL) {
    zxlogf(ERROR, "%s: CDC %s descriptor(s) not found", module_name,
           cdc_header_desc ? "ethernet" : cdc_eth_desc ? "header" : "ethernet and header");
    goto fail;
  }
  if (int_ep == NULL || tx_ep == NULL || rx_ep == NULL) {
    zxlogf(ERROR, "%s: missing one or more required endpoints\n", module_name);
    goto fail;
  }
  if (default_ifc == NULL) {
    zxlogf(ERROR, "%s: unable to find CDC default interface\n", module_name);
    goto fail;
  }
  if (data_ifc == NULL) {
    zxlogf(ERROR, "%s: unable to find CDC data interface\n", module_name);
    goto fail;
  }

  // Parse the information in the CDC descriptors
  if (!parse_cdc_header(cdc_header_desc)) {
    goto fail;
  }
  if (!parse_cdc_ethernet_descriptor(ecm_ctx, cdc_eth_desc)) {
    goto fail;
  }

  // Parse endpoint information
  copy_endpoint_info(&ecm_ctx->int_endpoint, int_ep);
  copy_endpoint_info(&ecm_ctx->tx_endpoint, tx_ep);
  copy_endpoint_info(&ecm_ctx->rx_endpoint, rx_ep);

  ecm_ctx->rx_endpoint_delay = ETHERNET_INITIAL_RECV_DELAY;
  ecm_ctx->tx_endpoint_delay = ETHERNET_INITIAL_TRANSMIT_DELAY;
  // Reset by selecting default interface followed by data interface. We can't start
  // queueing transactions until this is complete.
  usb_set_interface(&usb, default_ifc->bInterfaceNumber, default_ifc->bAlternateSetting);
  usb_set_interface(&usb, data_ifc->bInterfaceNumber, data_ifc->bAlternateSetting);

  // Allocate interrupt transaction buffer
  usb_request_t* int_buf;
  uint64_t req_size = ecm_ctx->parent_req_size + sizeof(usb_req_internal_t);
  zx_status_t alloc_result = usb_request_alloc(&int_buf, ecm_ctx->int_endpoint.max_packet_size,
                                               ecm_ctx->int_endpoint.addr, req_size);
  if (alloc_result != ZX_OK) {
    result = alloc_result;
    goto fail;
  }

  ecm_ctx->int_txn_buf = int_buf;

  // Allocate tx transaction buffers
  uint16_t tx_buf_sz = ecm_ctx->mtu;
#if MAX_TX_BUF_SZ < UINT16_MAX
  if (tx_buf_sz > MAX_TX_BUF_SZ) {
    zxlogf(ERROR, "%s: insufficient space for even a single tx buffer\n", module_name);
    goto fail;
  }
#endif
  size_t tx_buf_remain = MAX_TX_BUF_SZ;
  while (tx_buf_remain >= tx_buf_sz) {
    usb_request_t* tx_buf;
    zx_status_t alloc_result =
        usb_request_alloc(&tx_buf, tx_buf_sz, ecm_ctx->tx_endpoint.addr, req_size);
    tx_buf->direct = true;
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      goto fail;
    }

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
    // transmission when the endpoint max packet size is a factor of the total transmission size
    tx_buf->header.send_zlp = true;

    zx_status_t add_result =
        usb_req_list_add_head(&ecm_ctx->tx_txn_bufs, tx_buf, ecm_ctx->parent_req_size);
    ZX_DEBUG_ASSERT(add_result == ZX_OK);

    tx_buf_remain -= tx_buf_sz;
  }

  // Allocate rx transaction buffers
  uint32_t rx_buf_sz = ecm_ctx->mtu;
#if MAX_TX_BUF_SZ < UINT16_MAX
  if (rx_buf_sz > MAX_RX_BUF_SZ) {
    zxlogf(ERROR, "%s: insufficient space for even a single rx buffer\n", module_name);
    goto fail;
  }
#endif

  usb_request_complete_t complete = {
      .callback = usb_read_complete,
      .ctx = ecm_ctx,
  };
  size_t rx_buf_remain = MAX_RX_BUF_SZ;
  while (rx_buf_remain >= rx_buf_sz) {
    usb_request_t* rx_buf;
    zx_status_t alloc_result =
        usb_request_alloc(&rx_buf, rx_buf_sz, ecm_ctx->rx_endpoint.addr, req_size);
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      goto fail;
    }
    rx_buf->direct = true;
    usb_request_queue(&ecm_ctx->usb, rx_buf, &complete);
    rx_buf_remain -= rx_buf_sz;
  }

  // Kick off the handler thread
  int thread_result = thrd_create_with_name(&ecm_ctx->int_thread, ecm_int_handler_thread, ecm_ctx,
                                            "ecm_int_handler_thread");
  if (thread_result != thrd_success) {
    zxlogf(ERROR, "%s: failed to create interrupt handler thread (%d)\n", module_name,
           thread_result);
    goto fail;
  }

  // Add the device
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "usb-cdc-ecm",
      .ctx = ecm_ctx,
      .ops = &ecm_device_proto,
      .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
      .proto_ops = &ethernet_impl_ops,
  };
  result = device_add(ecm_ctx->usb_device, &args, &ecm_ctx->zxdev);
  if (result < 0) {
    zxlogf(ERROR, "%s: failed to add device: %d\n", module_name, (int)result);
    goto fail;
  }

  usb_desc_iter_release(&iter);
  return ZX_OK;

fail:
  usb_desc_iter_release(&iter);
  ecm_free(ecm_ctx);
  zxlogf(ERROR, "%s: failed to bind\n", module_name);
  return result;
}

static zx_driver_ops_t ecm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ecm_bind,
};

ZIRCON_DRIVER_BEGIN(ethernet_usb_cdc_ecm, ecm_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB), BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_COMM),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_CDC_SUBCLASS_ETHERNET),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0), ZIRCON_DRIVER_END(ethernet_usb_cdc_ecm)
