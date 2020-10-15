// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asix-88772b.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/connectivity/ethernet/drivers/asix-88772b/ethernet_ax88772b-bind.h"

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_IN_SIZE 16384
#define USB_BUF_OUT_SIZE 2048
#define INTR_REQ_SIZE 8
#define ETH_HEADER_SIZE 4
#define ETH_MTU 1500

#define ETHERNET_MAX_TRANSMIT_DELAY 100
#define ETHERNET_MAX_RECV_DELAY 100
#define ETHERNET_TRANSMIT_DELAY 10
#define ETHERNET_RECV_DELAY 10
#define ETHERNET_INITIAL_TRANSMIT_DELAY 0
#define ETHERNET_INITIAL_RECV_DELAY 0

typedef struct {
  zx_device_t* device;
  zx_device_t* usb_device;
  usb_protocol_t usb;

  uint8_t phy_id;
  uint8_t mac_addr[6];
  uint8_t status[INTR_REQ_SIZE];
  bool online;
  bool dead;
  uint8_t bulk_in_addr;
  uint8_t bulk_out_addr;

  // pool of free USB requests
  list_node_t free_read_reqs;
  list_node_t free_write_reqs;
  list_node_t free_intr_reqs;

  // List of netbufs that haven't been copied into a USB transaction yet. Should only contain
  // entries if free_write_reqs is empty.
  list_node_t pending_netbufs;

  uint64_t rx_endpoint_delay;  // wait time between 2 recv requests
  uint64_t tx_endpoint_delay;  // wait time between 2 transmit requests

  // callback interface to attached ethernet layer
  ethernet_ifc_protocol_t ifc;

  size_t parent_req_size;

  mtx_t mutex;
} ax88772b_t;

typedef struct txn_info {
  ethernet_netbuf_t netbuf;
  ethernet_impl_queue_tx_callback completion_cb;
  void* cookie;
  list_node_t node;
} txn_info_t;

static void complete_txn(txn_info_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->netbuf);
}

static void ax88772b_interrupt_complete(void* ctx, usb_request_t* request);
static void ax88772b_write_complete(void* ctx, usb_request_t* request);

static zx_status_t ax88772b_set_value(ax88772b_t* eth, uint8_t request, uint16_t value) {
  return usb_control_out(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, request,
                         value, 0, ZX_TIME_INFINITE, NULL, 0);
}

static zx_status_t ax88772b_get_value(ax88772b_t* eth, uint8_t request, uint16_t* value_addr) {
  return usb_control_in(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, request, 0, 0,
                        ZX_TIME_INFINITE, value_addr, sizeof(uint16_t), NULL);
}

static zx_status_t ax88772b_mdio_read(ax88772b_t* eth, uint8_t offset, uint16_t* value) {
  zx_status_t status = ax88772b_set_value(eth, ASIX_REQ_SW_SERIAL_MGMT_CTRL, 0);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_SW_SERIAL_MGMT_CTRL failed: %d", status);
    return status;
  }
  status =
      usb_control_in(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, ASIX_REQ_PHY_READ,
                     eth->phy_id, offset, ZX_TIME_INFINITE, value, sizeof(*value), NULL);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_PHY_READ failed: %d", status);
    return status;
  }
  status = ax88772b_set_value(eth, ASIX_REQ_HW_SERIAL_MGMT_CTRL, 0);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_HW_SERIAL_MGMT_CTRL failed: %d", status);
    return status;
  }

  return ZX_OK;
}

static zx_status_t ax88772b_wait_for_phy(ax88772b_t* eth) {
  for (int i = 0; i < 100; i++) {
    uint16_t bmsr;
    zx_status_t status = ax88772b_mdio_read(eth, ASIX_PHY_BMSR, &bmsr);
    if (status < 0) {
      zxlogf(ERROR, "ax88772b: ax88772b_mdio_read failed: %d", status);
      return status;
    }
    if (bmsr)
      return ZX_OK;
    usleep(50);
  }

  zxlogf(INFO, "ax88772b: ax88772b_wait_for_phy timeout");
  return ZX_ERR_TIMED_OUT;
}

static void queue_interrupt_requests_locked(ax88772b_t* eth) {
  usb_request_t* req;
  usb_request_complete_t complete = {
      .callback = ax88772b_interrupt_complete,
      .ctx = eth,
  };
  while ((req = usb_req_list_remove_head(&eth->free_intr_reqs, eth->parent_req_size)) != NULL) {
    usb_request_queue(&eth->usb, req, &complete);
  }
}

static void ax88772b_recv(ax88772b_t* eth, usb_request_t* request) {
  size_t len = request->response.actual;
  uint8_t* pkt;
  zx_status_t status = usb_request_mmap(request, (void*)&pkt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88772b: usb_request_mmap failed: %d", status);
    return;
  }

  while (len > ETH_HEADER_SIZE) {
    uint16_t length1 = (pkt[0] | (uint16_t)pkt[1] << 8) & 0x7FF;
    uint16_t length2 = (~(pkt[2] | (uint16_t)pkt[3] << 8)) & 0x7FF;
    pkt += ETH_HEADER_SIZE;
    len -= ETH_HEADER_SIZE;

    if (length1 != length2) {
      zxlogf(ERROR, "ax88772b: invalid header: length1: %u length2: %u", length1, length2);
      return;
    }

    if (length1 > len) {
      return;
    }

    ethernet_ifc_recv(&eth->ifc, pkt, length1, 0);
    pkt += length1;
    len -= length1;

    // align to uint16
    if (length1 & 1) {
      if (len == 0) {
        return;
      }
      pkt++;
      len--;
    }
  }
}

// Send a netbuf to the USB interface using the provided request
static zx_status_t ax88772b_send(ax88772b_t* eth, usb_request_t* request,
                                 ethernet_netbuf_t* netbuf) {
  size_t length = netbuf->data_size;

  if (length + ETH_HEADER_SIZE > USB_BUF_OUT_SIZE) {
    zxlogf(ERROR, "ax88772b: unsupported packet length %zu", length);
    return ZX_ERR_INVALID_ARGS;
  }

  // write 4 byte packet header
  uint8_t header[ETH_HEADER_SIZE];
  uint8_t lo = (uint8_t)length;
  uint8_t hi = (uint8_t)(length >> 8);
  header[0] = lo;
  header[1] = hi;
  header[2] = lo ^ 0xFF;
  header[3] = hi ^ 0xFF;

  usb_request_copy_to(request, header, ETH_HEADER_SIZE, 0);
  usb_request_copy_to(request, netbuf->data_buffer, length, ETH_HEADER_SIZE);
  request->header.length = length + ETH_HEADER_SIZE;

  zx_nanosleep(zx_deadline_after(ZX_USEC(eth->tx_endpoint_delay)));
  usb_request_complete_t complete = {
      .callback = ax88772b_write_complete,
      .ctx = eth,
  };
  usb_request_queue(&eth->usb, request, &complete);
  return ZX_OK;
}

static void ax88772b_read_complete(void* ctx, usb_request_t* request) {
  ax88772b_t* eth = (ax88772b_t*)ctx;

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  mtx_lock(&eth->mutex);
  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "ax88772b_read_complete usb_reset_endpoint");
    usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG,
           "ax88772b_read_complete Slowing down the requests by %d usec"
           " and resetting the recv endpoint\n",
           ETHERNET_RECV_DELAY);
    if (eth->rx_endpoint_delay < ETHERNET_MAX_RECV_DELAY) {
      eth->rx_endpoint_delay += ETHERNET_RECV_DELAY;
    }
    usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
  } else if ((request->response.status == ZX_OK) && eth->ifc.ops) {
    ax88772b_recv(eth, request);
  }

  if (eth->online) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(eth->rx_endpoint_delay)));
    usb_request_complete_t complete = {
        .callback = ax88772b_read_complete,
        .ctx = eth,
    };
    usb_request_queue(&eth->usb, request, &complete);
  } else {
    zx_status_t status = usb_req_list_add_head(&eth->free_read_reqs, request, eth->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
  mtx_unlock(&eth->mutex);
}

static void ax88772b_write_complete(void* ctx, usb_request_t* request) {
  ax88772b_t* eth = (ax88772b_t*)ctx;

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  mtx_lock(&eth->mutex);
  if (!list_is_empty(&eth->pending_netbufs)) {
    // If we have any netbufs that are waiting to be sent, reuse the request we just got back
    txn_info_t* txn = list_remove_head_type(&eth->pending_netbufs, txn_info_t, node);
    zx_status_t send_result = ax88772b_send(eth, request, &txn->netbuf);
    complete_txn(txn, send_result);
  } else {
    zx_status_t status =
        usb_req_list_add_tail(&eth->free_write_reqs, request, eth->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "ax88772b_write_complete usb_reset_endpoint");
    usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG,
           "ax88772b_write_complete Slowing down the requests by %d usec"
           " and resetting the transmit endpoint\n",
           ETHERNET_TRANSMIT_DELAY);
    if (eth->tx_endpoint_delay < ETHERNET_MAX_TRANSMIT_DELAY) {
      eth->tx_endpoint_delay += ETHERNET_TRANSMIT_DELAY;
    }
    usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
  }

  mtx_unlock(&eth->mutex);
}

static void ax88772b_interrupt_complete(void* ctx, usb_request_t* request) {
  ax88772b_t* eth = (ax88772b_t*)ctx;

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  mtx_lock(&eth->mutex);
  if (request->response.status == ZX_OK && request->response.actual == sizeof(eth->status)) {
    uint8_t status[INTR_REQ_SIZE];

    usb_request_copy_from(request, status, sizeof(status), 0);
    if (memcmp(eth->status, status, sizeof(eth->status))) {
      const uint8_t* b = status;
      zxlogf(DEBUG, "ax88772b: status changed: %02X %02X %02X %02X %02X %02X %02X %02X", b[0], b[1],
             b[2], b[3], b[4], b[5], b[6], b[7]);
      memcpy(eth->status, status, sizeof(eth->status));
      uint8_t bb = eth->status[2];
      bool online = (bb & 1) != 0;
      bool was_online = eth->online;
      eth->online = online;
      if (online && !was_online) {
        if (eth->ifc.ops) {
          ethernet_ifc_status(&eth->ifc, ETHERNET_STATUS_ONLINE);
        }

        // Now that we are online, queue all our read requests
        usb_req_internal_t* req_int;
        usb_req_internal_t* prev;
        usb_request_t* req;
        list_for_every_entry_safe (&eth->free_read_reqs, req_int, prev, usb_req_internal_t, node) {
          list_delete(&req_int->node);
          req = REQ_INTERNAL_TO_USB_REQ(req_int, eth->parent_req_size);
          usb_request_complete_t complete = {
              .callback = ax88772b_read_complete,
              .ctx = eth,
          };
          usb_request_queue(&eth->usb, req, &complete);
        }
      } else if (!online && was_online) {
        if (eth->ifc.ops) {
          ethernet_ifc_status(&eth->ifc, 0);
        }
      }
    }
  }

  zx_status_t status = usb_req_list_add_head(&eth->free_intr_reqs, request, eth->parent_req_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  queue_interrupt_requests_locked(eth);

  mtx_unlock(&eth->mutex);
}

static void ax88772b_queue_tx(void* ctx, uint32_t options, ethernet_netbuf_t* netbuf,
                              ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  ax88772b_t* eth = ctx;
  txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

  if (eth->dead) {
    complete_txn(txn, ZX_ERR_PEER_CLOSED);
    return;
  }

  zx_status_t status = ZX_OK;

  mtx_lock(&eth->mutex);

  list_node_t* node = list_remove_head(&eth->free_write_reqs);
  if (!node) {
    list_add_tail(&eth->pending_netbufs, &txn->node);
    mtx_unlock(&eth->mutex);
    return;
  }
  usb_req_internal_t* req_int = containerof(node, usb_req_internal_t, node);
  usb_request_t* request = REQ_INTERNAL_TO_USB_REQ(req_int, eth->parent_req_size);

  status = ax88772b_send(eth, request, netbuf);

  mtx_unlock(&eth->mutex);
  complete_txn(txn, status);
}

static void ax88772b_unbind(void* ctx) {
  ax88772b_t* eth = ctx;

  mtx_lock(&eth->mutex);
  eth->dead = true;
  mtx_unlock(&eth->mutex);

  // this must be last since this can trigger releasing the device
  device_unbind_reply(eth->device);
}

static void ax88772b_free(ax88772b_t* eth) {
  usb_request_t* req;
  while ((req = usb_req_list_remove_head(&eth->free_read_reqs, eth->parent_req_size)) != NULL) {
    usb_request_release(req);
  }
  while ((req = usb_req_list_remove_head(&eth->free_write_reqs, eth->parent_req_size)) != NULL) {
    usb_request_release(req);
  }
  while ((req = usb_req_list_remove_head(&eth->free_intr_reqs, eth->parent_req_size)) != NULL) {
    usb_request_release(req);
  }
  free(eth);
}

static void ax88772b_release(void* ctx) {
  ax88772b_t* eth = ctx;
  ax88772b_free(eth);
}

static zx_protocol_device_t ax88772b_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ax88772b_unbind,
    .release = ax88772b_release,
};

static zx_status_t ax88772b_query(void* ctx, uint32_t options, ethernet_info_t* info) {
  ax88772b_t* eth = ctx;

  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  memset(info, 0, sizeof(*info));
  ZX_DEBUG_ASSERT(USB_BUF_OUT_SIZE - ETH_HEADER_SIZE >= ETH_MTU);
  info->mtu = ETH_MTU;
  memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));
  info->netbuf_size = sizeof(txn_info_t);

  return ZX_OK;
}

static void ax88772b_stop(void* ctx) {
  ax88772b_t* eth = ctx;
  mtx_lock(&eth->mutex);
  eth->ifc.ops = NULL;
  mtx_unlock(&eth->mutex);
}

static zx_status_t ax88772b_start(void* ctx, const ethernet_ifc_protocol_t* ifc) {
  ax88772b_t* eth = ctx;
  zx_status_t status = ZX_OK;

  mtx_lock(&eth->mutex);
  if (eth->ifc.ops) {
    status = ZX_ERR_BAD_STATE;
  } else {
    eth->ifc = *ifc;
    ethernet_ifc_status(&eth->ifc, eth->online ? ETHERNET_STATUS_ONLINE : 0);
  }
  mtx_unlock(&eth->mutex);

  return status;
}

static zx_status_t ax88772b_set_promisc(ax88772b_t* eth, bool on) {
  uint16_t rx_bits;
  zx_status_t status = ax88772b_get_value(eth, ASIX_REQ_RX_CONTROL_READ, &rx_bits);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_RX_CONTROL_READ failed; set_promisc() will fail.");
    return status;
  }
  if (on) {
    rx_bits |= ASIX_RX_CTRL_PRO;
  } else {
    rx_bits &= ~ASIX_RX_CTRL_PRO;
  }
  status = ax88772b_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, rx_bits);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_RX_CONTROL_WRITE failed");
  }

  return status;
}

static zx_status_t ax88772b_set_param(void* ctx, uint32_t param, int32_t value, const void* data,
                                      size_t data_size) {
  ax88772b_t* eth = ctx;
  zx_status_t status = ZX_OK;

  mtx_lock(&eth->mutex);

  switch (param) {
    case ETHERNET_SETPARAM_PROMISC:
      status = ax88772b_set_promisc(eth, (bool)value);
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
  }

  mtx_unlock(&eth->mutex);
  return status;
}

static ethernet_impl_protocol_ops_t ethernet_impl_ops = {
    .query = ax88772b_query,
    .stop = ax88772b_stop,
    .start = ax88772b_start,
    .queue_tx = ax88772b_queue_tx,
    .set_param = ax88772b_set_param,
};

static int ax88772b_start_thread(void* arg) {
  ax88772b_t* eth = (ax88772b_t*)arg;

  // set some GPIOs
  zx_status_t status =
      ax88772b_set_value(eth, ASIX_REQ_GPIOS, ASIX_GPIO_GPO2EN | ASIX_GPIO_GPO_2 | ASIX_GPIO_RSE);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_WRITE_GPIOS failed: %d", status);
    goto fail;
  }

  // select the PHY
  uint8_t phy_addr[2];
  status =
      usb_control_in(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, ASIX_REQ_PHY_ADDR,
                     0, 0, ZX_TIME_INFINITE, &phy_addr, sizeof(phy_addr), NULL);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_READ_PHY_ADDR failed: %d", status);
    goto fail;
  }
  eth->phy_id = phy_addr[1];
  uint16_t embed_phy = (eth->phy_id & 0x1F) == 0x10 ? 1 : 0;
  status = ax88772b_set_value(eth, ASIX_REQ_SW_PHY_SELECT, embed_phy);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_SW_PHY_SELECT failed: %d", status);
    goto fail;
  }

  // Reset
  status = ax88772b_set_value(eth, ASIX_REQ_SW_RESET, ASIX_RESET_PRL | ASIX_RESET_IPPD);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_SW_RESET failed: %d", status);
    goto fail;
  }
  status = ax88772b_set_value(eth, ASIX_REQ_SW_RESET, 0);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_SW_RESET failed: %d", status);
    goto fail;
  }
  status =
      ax88772b_set_value(eth, ASIX_REQ_SW_RESET, (embed_phy ? ASIX_RESET_IPRL : ASIX_RESET_PRTE));
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_SW_RESET failed: %d", status);
    goto fail;
  }
  status = ax88772b_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, 0);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_RX_CONTROL_WRITE failed: %d", status);
    goto fail;
  }

  status = ax88772b_wait_for_phy(eth);
  if (status < 0) {
    goto fail;
  }

  uint16_t medium = ASIX_MEDIUM_MODE_FD | ASIX_MEDIUM_MODE_AC | ASIX_MEDIUM_MODE_RFC |
                    ASIX_MEDIUM_MODE_TFC | ASIX_MEDIUM_MODE_JFE | ASIX_MEDIUM_MODE_RE |
                    ASIX_MEDIUM_MODE_PS;
  status = ax88772b_set_value(eth, ASIX_REQ_MEDIUM_MODE, medium);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_MEDIUM_MODE failed: %d", status);
    goto fail;
  }

  status = usb_control_out(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                           ASIX_REQ_IPG_WRITE, ASIX_IPG_DEFAULT | (ASIX_IPG1_DEFAULT << 8),
                           ASIX_IPG2_DEFAULT, ZX_TIME_INFINITE, NULL, 0);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_IPG_WRITE failed: %d", status);
    goto fail;
  }

  status = ax88772b_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE,
                              ASIX_RX_CTRL_AMALL | ASIX_RX_CTRL_AB | ASIX_RX_CTRL_S0);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_RX_CONTROL_WRITE failed: %d", status);
    goto fail;
  }

  status = usb_control_in(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                          ASIX_REQ_NODE_ID_READ, 0, 0, ZX_TIME_INFINITE, eth->mac_addr,
                          sizeof(eth->mac_addr), NULL);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: ASIX_REQ_NODE_ID_READ failed: %d", status);
    goto fail;
  }
  zxlogf(INFO, "ax88772b: MAC address: %02x:%02x:%02x:%02x:%02x:%02x", eth->mac_addr[0],
         eth->mac_addr[1], eth->mac_addr[2], eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "ax88772b",
      .ctx = eth,
      .ops = &ax88772b_device_proto,
      .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
      .proto_ops = &ethernet_impl_ops,
  };

  status = device_add(eth->usb_device, &args, &eth->device);
  if (status < 0) {
    zxlogf(ERROR, "ax88772b: failed to create device: %d", status);
    goto fail;
  }

  mtx_lock(&eth->mutex);
  queue_interrupt_requests_locked(eth);
  mtx_unlock(&eth->mutex);
  return ZX_OK;

fail:
  ax88772b_free(eth);
  return status;
}

static zx_status_t ax88772b_bind(void* ctx, zx_device_t* device) {
  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
    return result;
  }

  // find our endpoints
  usb_desc_iter_t iter;
  result = usb_desc_iter_init(&usb, &iter);
  if (result < 0)
    return result;

  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->bNumEndpoints != 3) {
    usb_desc_iter_release(&iter);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;
  uint8_t intr_addr = 0;

  usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
  while (endp) {
    if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_out_addr = endp->bEndpointAddress;
      }
    } else {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_in_addr = endp->bEndpointAddress;
      } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
        intr_addr = endp->bEndpointAddress;
      }
    }
    endp = usb_desc_iter_next_endpoint(&iter);
  }
  usb_desc_iter_release(&iter);

  if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
    zxlogf(ERROR, "ax88772b: ax88772b_bind could not find endpoints");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ax88772b_t* eth = calloc(1, sizeof(ax88772b_t));
  if (!eth) {
    zxlogf(ERROR, "ax88772b: Not enough memory for ax88772b_t");
    return ZX_ERR_NO_MEMORY;
  }

  list_initialize(&eth->free_read_reqs);
  list_initialize(&eth->free_write_reqs);
  list_initialize(&eth->free_intr_reqs);
  list_initialize(&eth->pending_netbufs);

  eth->usb_device = device;
  memcpy(&eth->usb, &usb, sizeof(eth->usb));

  eth->parent_req_size = usb_get_request_size(&eth->usb);
  uint64_t req_size = eth->parent_req_size + sizeof(usb_req_internal_t);

  eth->bulk_in_addr = bulk_in_addr;
  eth->bulk_out_addr = bulk_out_addr;

  eth->rx_endpoint_delay = ETHERNET_INITIAL_RECV_DELAY;
  eth->tx_endpoint_delay = ETHERNET_INITIAL_TRANSMIT_DELAY;
  zx_status_t status = ZX_OK;
  for (int i = 0; i < READ_REQ_COUNT; i++) {
    usb_request_t* req;
    status = usb_request_alloc(&req, USB_BUF_IN_SIZE, bulk_in_addr, req_size);
    if (status != ZX_OK) {
      goto fail;
    }
    status = usb_req_list_add_head(&eth->free_read_reqs, req, eth->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
  for (int i = 0; i < WRITE_REQ_COUNT; i++) {
    usb_request_t* req;
    status = usb_request_alloc(&req, USB_BUF_OUT_SIZE, bulk_out_addr, req_size);
    if (status != ZX_OK) {
      goto fail;
    }
    status = usb_req_list_add_head(&eth->free_write_reqs, req, eth->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
  for (int i = 0; i < INTR_REQ_COUNT; i++) {
    usb_request_t* req;
    status = usb_request_alloc(&req, INTR_REQ_SIZE, intr_addr, req_size);
    if (status != ZX_OK) {
      goto fail;
    }
    status = usb_req_list_add_head(&eth->free_intr_reqs, req, eth->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  thrd_t thread;
  thrd_create_with_name(&thread, ax88772b_start_thread, eth, "ax88772b_start_thread");
  thrd_detach(thread);

  return ZX_OK;

fail:
  zxlogf(ERROR, "ax88772b: ax88772b_bind failed: %d", status);
  ax88772b_free(eth);
  return status;
}

static zx_driver_ops_t ax88772b_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ax88772b_bind,
};

ZIRCON_DRIVER(ethernet_ax88772b, ax88772b_driver_ops, "zircon", "0.1")
