// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qmi-usb-transport.h"
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <lib/sync/completion.h>
#include <usb/usb.h>

#include <fuchsia/hardware/telephony/transport/c/fidl.h>

#include <zircon/device/qmi-transport.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _ALL_SOURCE
#include <threads.h>
#include <usb/usb-request.h>

// The maximum amount of memory we are willing to allocate to transaction
// buffers
#define MAX_TX_BUF_SZ 32768
#define MAX_RX_BUF_SZ 32768

#define ETHMAC_MAX_TRANSMIT_DELAY 100
#define ETHMAC_MAX_RECV_DELAY 100
#define ETHMAC_TRANSMIT_DELAY 10
#define ETHMAC_RECV_DELAY 10
#define ETHMAC_INITIAL_TRANSMIT_DELAY 0
#define ETHMAC_INITIAL_RECV_DELAY 0

#define ETHERNET_FRAME_OFFSET 14

typedef struct txn_info {
  ethmac_netbuf_t netbuf;
  list_node_t node;
} txn_info_t;

// qmi usb transport device
typedef struct qmi_ctx {
  // Interrupt handling
  usb_request_t* int_txn_buf;
  sync_completion_t completion;
  thrd_t int_thread;

  uint16_t max_packet_size;

  // Port to watch for QMI messages on
  zx_handle_t channel_port;
  zx_handle_t channel;

  usb_protocol_t usb;
  zx_device_t* usb_device;
  zx_device_t* zxdev;
  size_t parent_req_size;

  // Ethernet
  zx_device_t* eth_zxdev;

  mtx_t ethmac_mutex;
  ethmac_ifc_t ethmac_ifc;

  // Device attributes
  uint8_t mac_addr[ETH_MAC_SIZE];
  uint16_t mtu;

  // Connection attributes
  bool online;
  uint32_t ds_bps;
  uint32_t us_bps;

  // Send context
  mtx_t tx_mutex;
  uint8_t tx_endpoint_addr;
  uint16_t endpoint_size;
  list_node_t tx_txn_bufs;       // list of usb_request_t
  list_node_t tx_pending_infos;  // list of txn_info_t
  bool unbound;  // set to true when device is going away. Guarded by tx_mutex
  uint64_t tx_endpoint_delay;  // wait time between 2 transmit requests

  // Receive context
  uint8_t rx_endpoint_addr;
  uint64_t rx_endpoint_delay;  // wait time between 2 recv requests
} qmi_ctx_t;

static void usb_write_complete(void* ctx, usb_request_t* request);

static zx_status_t set_channel(void* ctx, zx_handle_t channel) {
  ZX_DEBUG_ASSERT(ctx);
  zxlogf(INFO, "qmi-usb-transport: getting channel from transport\n");
  qmi_ctx_t* qmi_ctx = ctx;
  zx_status_t result = ZX_OK;

  if (qmi_ctx->channel != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: already bound, failing\n");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: invalid channel handle\n");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    qmi_ctx->channel = channel;
  }
  return result;
}

static zx_status_t queue_request(qmi_ctx_t* ctx, const uint8_t* data,
                                 size_t length, usb_request_t* req) {
  req->header.length = length;
  if (length < 41) {
    zxlogf(ERROR, "qmi-usb-transport: length is too short (length: %zx) \n",
           length);
    return ZX_ERR_IO;
  }

  if (length > 1024) {
    zxlogf(ERROR,
           "qmi-usb-transport: length is greater than buffer size. (length: "
           "%zx) \n",
           length);
    return ZX_ERR_IO;
  }

  // Check if this in an arp frame, short-circuit return a synthetic one if so
  const uint8_t* eth = &data[ETHERNET_FRAME_OFFSET];
  if ((eth[0] == 0x00) &&                      // hardware type
      (eth[1] == 0x01) && (eth[2] == 0x08) &&  // protocol
      (eth[3] == 0x00) && (eth[4] == 0x06) &&  // hardware len
      (eth[5] == 0x04) &&                      // protocol len
      (eth[6] == 0x00) &&                      // arp request op
      (eth[7] == 0x01)) {
    uint8_t read_data[] = {
        // ethernet frame
        0x62, 0x77, 0x62, 0x62, 0x77, 0x62,  // destination mac addr (bwb)
        0x79, 0x61, 0x6B, 0x79, 0x61, 0x6B,  // source mac addr (yak)
        0x08, 0x06,                          // arp ethertype
        // data payload
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02,  // ARP header
        0x79, 0x61, 0x6B, 0x79, 0x61, 0x6B,              // yak mac addr
        eth[24], eth[25], eth[26], eth[27],              // swapped IP addr
        0x62, 0x77, 0x62, 0x62, 0x77, 0x62,              // bwb mac addr
        eth[14], eth[15], eth[16], eth[17],              // swapped IP addr
    };

    zx_nanosleep(zx_deadline_after(ZX_USEC(ctx->tx_endpoint_delay)));
    mtx_lock(&ctx->ethmac_mutex);
    if (ctx->ethmac_ifc.ops) {
      ethmac_ifc_recv(&ctx->ethmac_ifc, read_data, sizeof(read_data), 0);
    }
    mtx_unlock(&ctx->ethmac_mutex);
    return ZX_OK;
  }

  ssize_t ip_length = ((eth[2] << 8) + eth[3]);
  if (ip_length > (ssize_t)length) {
    zxlogf(ERROR,
           "qmi-usb-transport: length of IP packet is more than the ethernet "
           "frame! %zx/%zd \n",
           ip_length, length);
    return ZX_ERR_IO;
  }

  ssize_t bytes_copied = usb_request_copy_to(req, eth, ip_length, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR,
           "qmi-usb-transport: failed to copy data into send txn (error %zd)\n",
           bytes_copied);
    return ZX_ERR_IO;
  }

  usb_request_complete_t complete = {
      .callback = usb_write_complete,
      .ctx = ctx,
  };
  usb_request_queue(&ctx->usb, req, &complete);
  return ZX_OK;
}

static zx_status_t send_locked(qmi_ctx_t* ctx, ethmac_netbuf_t* netbuf) {
  const uint8_t* byte_data = netbuf->data_buffer;
  size_t length = netbuf->data_size;

  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* tx_req =
      usb_req_list_remove_head(&ctx->tx_txn_bufs, ctx->parent_req_size);
  if (tx_req == NULL) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(ctx->tx_endpoint_delay)));
  zx_status_t status;
  if ((status = queue_request(ctx, byte_data, length, tx_req)) != ZX_OK) {
    zx_status_t add_status =
        usb_req_list_add_tail(&ctx->tx_txn_bufs, tx_req, ctx->parent_req_size);
    ZX_DEBUG_ASSERT(add_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

static void qmi_update_online_status(qmi_ctx_t* ctx, bool is_online) {
  mtx_lock(&ctx->ethmac_mutex);
  if ((is_online && ctx->online) || (!is_online && !ctx->online)) {
    goto done;
  }

  if (is_online) {
    zxlogf(INFO, "qmi-usb-transport: connected to network\n");
    ctx->online = true;
    if (ctx->ethmac_ifc.ops) {
      ethmac_ifc_status(&ctx->ethmac_ifc,
                        ctx->online ? ETHMAC_STATUS_ONLINE : 0);
    } else {
      zxlogf(ERROR, "qmi-usb-transport: not connected to ethermac interface\n");
    }
  } else {
    zxlogf(INFO, "qmi-usb-transport: no connection to network\n");
    ctx->online = false;
    if (ctx->ethmac_ifc.ops) {
      ethmac_ifc_status(&ctx->ethmac_ifc, 0);
    }
  }

done:
  mtx_unlock(&ctx->ethmac_mutex);
}

static inline zx_status_t set_async_wait(qmi_ctx_t* ctx) {
  zx_status_t status = zx_object_wait_async(
      ctx->channel, ctx->channel_port, CHANNEL_MSG,
      ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
  return status;
}

#define REPLY(x) fuchsia_hardware_telephony_transport_Qmi##x##_reply

static zx_status_t fidl_SetChannel(void* ctx, zx_handle_t transport, fidl_txn_t* txn) {
  zx_status_t status = ZX_OK;
  qmi_ctx_t* qmi_ctx = ctx;
  fuchsia_hardware_telephony_transport_Qmi_SetChannel_Result res;
  zx_status_t set_channel_res = set_channel(ctx, transport);
  if (set_channel_res == ZX_OK) {
    res.tag = 0;
    res.response._reserved = 0;
  } else {
    res.tag = 1;
    res.err = set_channel_res;
  }
  status = REPLY(SetChannel)(txn, &res);
  if (status != ZX_OK) {
    goto done;
  }
  if (set_channel_res == ZX_OK) {
    status = set_async_wait(qmi_ctx);
    if (status != ZX_OK) {
    zx_handle_close(qmi_ctx->channel);
    }
  }
done:
  return status;
}

static zx_status_t fidl_SetNetworkStatus(void* ctx, bool connected, fidl_txn_t* txn){
  qmi_ctx_t* qmi_ctx = ctx;
  qmi_update_online_status(qmi_ctx, connected);
  return REPLY(SetNetwork)(txn);
}
#undef REPLY

static fuchsia_hardware_telephony_transport_Qmi_ops_t fidl_ops = {
  .SetChannel = fidl_SetChannel,
  .SetNetwork = fidl_SetNetworkStatus,
};

static zx_status_t qmi_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  zx_status_t status = fuchsia_hardware_telephony_transport_Qmi_dispatch(ctx, txn, msg, &fidl_ops);
  return status;
}

static void qmi_release(void* ctx) {
  zxlogf(INFO, "qmi-usb-transport: releasing device\n");
  qmi_ctx_t* qmi_ctx = ctx;
  free(qmi_ctx);
}

static void qmi_unbind(void* ctx) {
  qmi_ctx_t* qmi_ctx = ctx;
  zx_status_t result = ZX_OK;
  result = device_remove(qmi_ctx->zxdev);
  if (result != ZX_OK) {
    zxlogf(
        ERROR,
        "Failed to unbind qmi-usb-transport driver. Cannot remove device: %u\n",
        result);
  }
}

static zx_status_t qmi_ethmac_query(void* ctx, uint32_t options,
                                    ethmac_info_t* info) {
  qmi_ctx_t* eth = ctx;

  zxlogf(INFO, "qmi-usb-transport: %s called\n", __FUNCTION__);

  // No options are supported
  if (options) {
    zxlogf(ERROR, "qmi-usb-transport: unexpected options to ethmac_query\n");
    return ZX_ERR_INVALID_ARGS;
  }

  memset(info, 0, sizeof(*info));
  info->mtu = 1024;
  memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));
  info->netbuf_size = sizeof(txn_info_t);

  return ZX_OK;
}

static zx_status_t qmi_ethmac_start(void* ctx_cookie, const ethmac_ifc_t* ifc) {
  zxlogf(INFO, "qmi-usb-transport: %s called\n", __FUNCTION__);
  qmi_ctx_t* ctx = ctx_cookie;
  zx_status_t status = ZX_OK;

  mtx_lock(&ctx->ethmac_mutex);
  if (ctx->ethmac_ifc.ops) {
    status = ZX_ERR_ALREADY_BOUND;
  } else {
    ctx->ethmac_ifc = *ifc;
    ethmac_ifc_status(&ctx->ethmac_ifc, ctx->online ? ETHMAC_STATUS_ONLINE : 0);
  }

  mtx_unlock(&ctx->ethmac_mutex);
  return status;
}

static zx_status_t qmi_ethmac_set_param(void* cookie, uint32_t param,
                                        int32_t value, const void* data,
                                        size_t data_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void qmi_ethmac_stop(void* cookie) {
  zxlogf(INFO, "qmi-usb-transport: %s called\n", __FUNCTION__);
  qmi_ctx_t* ctx = cookie;
  mtx_lock(&ctx->ethmac_mutex);
  ctx->ethmac_ifc.ops = NULL;
  mtx_unlock(&ctx->ethmac_mutex);
}

static zx_status_t qmi_ethmac_queue_tx(void* cookie, uint32_t options,
                                       ethmac_netbuf_t* netbuf) {
  qmi_ctx_t* ctx = cookie;
  size_t length = netbuf->data_size;
  zx_status_t status;

  // TODO mtu better
  if (length > 1024 || length == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  mtx_lock(&ctx->tx_mutex);
  if (ctx->unbound) {
    status = ZX_ERR_IO_NOT_PRESENT;
  } else {
    status = send_locked(ctx, netbuf);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // No buffers available, queue it up
      txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
      list_add_tail(&ctx->tx_pending_infos, &txn->node);
    }
  }

  mtx_unlock(&ctx->tx_mutex);
  return status;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = qmi_ethmac_query,
    .stop = qmi_ethmac_stop,
    .start = qmi_ethmac_start,
    .queue_tx = qmi_ethmac_queue_tx,
    .set_param = qmi_ethmac_set_param,
};

static zx_protocol_device_t qmi_ops = {
    .version = DEVICE_OPS_VERSION,
    .release = qmi_release,
    .unbind = qmi_unbind,
    .message = qmi_message,
};

static zx_protocol_device_t eth_qmi_ops = {
    .version = DEVICE_OPS_VERSION,
};

static void qmi_handle_interrupt(qmi_ctx_t* qmi_ctx, usb_request_t* request) {
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(ERROR, "qmi-usb-transport: ignored interrupt (size = %ld)\n",
           (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req;
  usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);

  uint16_t packet_size = qmi_ctx->max_packet_size;
  if (packet_size > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d\n", packet_size);
    return;
  }
  uint8_t buffer[packet_size];
  zx_status_t status;

  switch (usb_req.bNotification) {
    case USB_CDC_NC_RESPONSE_AVAILABLE:
      status = usb_control_in(
          &qmi_ctx->usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          USB_CDC_GET_ENCAPSULATED_RESPONSE, 0, QMI_INTERFACE_NUM,
          ZX_TIME_INFINITE, buffer, packet_size, NULL);
      if (!qmi_ctx->channel) {
        zxlogf(
            WARN,
            "qmi-usb-transport: recieving USB CDC frames without a channel\n");
        return;
      }
      status = zx_channel_write(qmi_ctx->channel, 0, buffer, sizeof(buffer),
                                NULL, 0);
      if (status < 0) {
        zxlogf(ERROR,
               "qmi-usb-transport: failed to write message to channel: %s\n",
               zx_status_get_string(status));
      }
      return;
    default:
      zxlogf(ERROR,
             "qmi-usb-transport: Unknown Notification Type for QMI: %d\n",
             usb_req.bNotification);
  }
}

static void qmi_interrupt_cb(void* ctx, usb_request_t* req) {
  qmi_ctx_t* qmi_ctx = (qmi_ctx_t*)ctx;

  zx_port_packet_t packet = {};
  packet.key = INTERRUPT_MSG;
  zx_port_queue(qmi_ctx->channel_port, &packet);
}

static int qmi_transport_thread(void* cookie) {
  qmi_ctx_t* ctx = cookie;
  usb_request_t* txn = ctx->int_txn_buf;

  usb_request_complete_t complete = {
      .callback = qmi_interrupt_cb,
      .ctx = ctx,
  };
  usb_request_queue(&ctx->usb, txn, &complete);
  if (ctx->max_packet_size > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d\n",
           ctx->max_packet_size);
    return ZX_ERR_IO_REFUSED;
  }
  uint8_t buffer[ctx->max_packet_size];
  uint32_t length = sizeof(buffer);
  zx_port_packet_t packet;
  while (true) {
    zx_status_t status =
        zx_port_wait(ctx->channel_port, ZX_TIME_INFINITE, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "qmi-usb-transport: timed out: %s\n",
             zx_status_get_string(status));
    } else {
      if (packet.key == CHANNEL_MSG) {
        if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
          zxlogf(INFO, "qmi-usb-transport: channel closed\n");
          status = zx_handle_close(ctx->channel);
          ctx->channel = ZX_HANDLE_INVALID;
          continue;
        }
        status = zx_channel_read(ctx->channel, 0, buffer, NULL, sizeof(buffer),
                                 0, &length, NULL);
        if (status != ZX_OK) {
          zxlogf(ERROR, "qmi-usb-transport: failed to read channel: %s\n",
                 zx_status_get_string(status));
          return status;
        }
        status = usb_control_out(
            &ctx->usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
            USB_CDC_SEND_ENCAPSULATED_COMMAND, 0, 8, ZX_TIME_INFINITE, buffer,
            length);
        if (status != ZX_OK) {
          zxlogf(
              ERROR,
              "qmi-usb-transport: got an bad status from usb_control_out: %s\n",
              zx_status_get_string(status));
          return status;
        }
        status = set_async_wait(ctx);
        if (status != ZX_OK) {
          return status;
        }
      } else if (packet.key == INTERRUPT_MSG) {
        if (txn->response.status == ZX_OK) {
          qmi_handle_interrupt(ctx, txn);
          usb_request_complete_t complete = {
              .callback = qmi_interrupt_cb,
              .ctx = ctx,
          };
          usb_request_queue(&ctx->usb, txn, &complete);
        } else if (txn->response.status == ZX_ERR_PEER_CLOSED ||
                   txn->response.status == ZX_ERR_IO_NOT_PRESENT) {
          zxlogf(INFO,
                 "qmi-usb-transport: terminating interrupt handling thread\n");
          return txn->response.status;
        }
      }
    }
  }
}

// Note: the assumption made here is that no rx transmissions will be processed
// in parallel, so we do not maintain an rx mutex.
static void usb_recv(qmi_ctx_t* ctx, usb_request_t* request) {
  size_t len = request->response.actual;

  uint8_t* read_data;
  zx_status_t status = usb_request_mmap(request, (void*)&read_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: usb_request_mmap failed with status %d\n",
           status);
    return;
  }

  if (len > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: recieved usb packet is too large: %zd\n",
           len);
    return;
  }

  uint8_t send_data[len + ETHERNET_FRAME_OFFSET];  // woo! VLA!
  send_data[0] = 0x62;                             // destination mac addr
  send_data[1] = 0x77;
  send_data[2] = 0x62;
  send_data[3] = 0x62;
  send_data[4] = 0x77;
  send_data[5] = 0x62;

  send_data[6] = 0x79;  // source mac addr
  send_data[7] = 0x61;
  send_data[8] = 0x6B;
  send_data[9] = 0x79;
  send_data[10] = 0x61;
  send_data[11] = 0x6B;

  send_data[12] = 0x08;
  send_data[13] = 0x00;

  memcpy(&send_data[ETHERNET_FRAME_OFFSET], read_data, len);
  mtx_lock(&ctx->ethmac_mutex);
  if (ctx->ethmac_ifc.ops) {
    ethmac_ifc_recv(&ctx->ethmac_ifc, send_data, len + ETHERNET_FRAME_OFFSET,
                    0);
  }
  mtx_unlock(&ctx->ethmac_mutex);
}

static void usb_read_complete(void* context, usb_request_t* request) {
  qmi_ctx_t* ctx = context;

  if (request->response.status != ZX_OK) {
    zxlogf(ERROR,
           "qmi-usb-transport: usb_read_complete called with status %d\n",
           (int)request->response.status);
  }

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "qmi-usb-transport: resetting receive endpoint\n");
    usb_reset_endpoint(&ctx->usb, ctx->rx_endpoint_addr);
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    if (ctx->rx_endpoint_delay < ETHMAC_MAX_RECV_DELAY) {
      ctx->rx_endpoint_delay += ETHMAC_RECV_DELAY;
    }
    zxlogf(ERROR,
           "qmi-usb-transport: slowing down the requests by %d usec."
           "Resetting the recv endpoint\n",
           ETHMAC_RECV_DELAY);
    usb_reset_endpoint(&ctx->usb, ctx->rx_endpoint_addr);
  } else if (request->response.status == ZX_OK) {
    usb_recv(ctx, request);
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(ctx->rx_endpoint_delay)));

  usb_request_complete_t complete = {
      .callback = usb_read_complete,
      .ctx = ctx,
  };
  usb_request_queue(&ctx->usb, request, &complete);
}

static void usb_write_complete(void* context, usb_request_t* request) {
  qmi_ctx_t* ctx = context;

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  mtx_lock(&ctx->tx_mutex);

  // Return transmission buffer to pool
  zx_status_t status =
      usb_req_list_add_tail(&ctx->tx_txn_bufs, request, ctx->parent_req_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "qmi-usb-transport: resetting transmit endpoint\n");
    usb_reset_endpoint(&ctx->usb, ctx->tx_endpoint_addr);
  }

  if (request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(ERROR,
           "qmi-usb-transport: slowing down the requests by %d usec."
           "Resetting the transmit endpoint\n",
           ETHMAC_TRANSMIT_DELAY);
    if (ctx->tx_endpoint_delay < ETHMAC_MAX_TRANSMIT_DELAY) {
      ctx->tx_endpoint_delay += ETHMAC_TRANSMIT_DELAY;
    }
    usb_reset_endpoint(&ctx->usb, ctx->tx_endpoint_addr);
  }

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

  mtx_lock(&ctx->ethmac_mutex);
  if (additional_tx_queued && ctx->ethmac_ifc.ops) {
    ethmac_ifc_complete_tx(&ctx->ethmac_ifc, &txn->netbuf, send_status);
  }
  mtx_unlock(&ctx->ethmac_mutex);

  // When the interface is offline, the transaction will complete with status
  // set to ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

static zx_status_t qmi_bind(void* ctx, zx_device_t* device) {
  zx_status_t status;
  qmi_ctx_t* qmi_ctx;
  usb_request_t* int_buf = NULL;
  if ((qmi_ctx = calloc(1, sizeof(qmi_ctx_t))) == NULL) {
    return ZX_ERR_NO_MEMORY;
  }

  // Set up USB stuff
  usb_protocol_t usb;
  status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: get protocol failed: %s\n",
           zx_status_get_string(status));
    goto fail;
  }

  // Initialize context
  qmi_ctx->usb_device = device;
  memcpy(&qmi_ctx->usb, &usb, sizeof(qmi_ctx->usb));
  list_initialize(&qmi_ctx->tx_txn_bufs);
  list_initialize(&qmi_ctx->tx_pending_infos);
  mtx_init(&qmi_ctx->ethmac_mutex, mtx_plain);
  mtx_init(&qmi_ctx->tx_mutex, mtx_plain);

  qmi_ctx->parent_req_size = usb_get_request_size(&qmi_ctx->usb);
  uint64_t req_size = qmi_ctx->parent_req_size + sizeof(usb_req_internal_t);
  ZX_DEBUG_ASSERT(qmi_ctx->parent_req_size != 0);

  // find our endpoints
  usb_desc_iter_t iter;
  zx_status_t result = usb_desc_iter_init(&usb, &iter);
  if (result < 0) {
    goto fail;
  }

  // QMI needs to bind to interface QMI_INTERFACE_NUM on current hardware.
  // Ignore the others for now.
  // TODO generic way of describing usb interfaces
  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->bInterfaceNumber != QMI_INTERFACE_NUM) {
    usb_desc_iter_release(&iter);
    status = ZX_ERR_NOT_SUPPORTED;
    goto failnoerr;  // this is not a big deal, just don't bind
  }

  if (intf->bNumEndpoints != 3) {
    zxlogf(ERROR,
           "qmi-usb-transport: interface does not have the required 3 "
           "endpoints\n");
    usb_desc_iter_release(&iter);
    status = ZX_ERR_NOT_SUPPORTED;
    goto fail;
  }

  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;
  uint8_t intr_addr = 0;
  uint16_t intr_max_packet = 0;
  uint16_t bulk_max_packet = 0;

  usb_descriptor_header_t* desc = usb_desc_iter_next(&iter);
  while (desc) {
    if (desc->bDescriptorType == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* endp = (void*)desc;
      if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
        if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
          bulk_out_addr = endp->bEndpointAddress;
          bulk_max_packet = usb_ep_max_packet(endp);
        }
      } else {
        if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
          bulk_in_addr = endp->bEndpointAddress;
        } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
          intr_addr = endp->bEndpointAddress;
          intr_max_packet = usb_ep_max_packet(endp);
        }
      }
    }
    desc = usb_desc_iter_next(&iter);
  }
  usb_desc_iter_release(&iter);

  if (bulk_in_addr == 0 || bulk_out_addr == 0 || intr_addr == 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to find one of the usb endpoints");
    goto fail;
  }

  if (intr_max_packet < 1 || bulk_max_packet < 1) {
    zxlogf(ERROR,
           "qmi-usb-transport: failed to find reasonable max packet sizes");
    goto fail;
  }

  qmi_ctx->rx_endpoint_delay = ETHMAC_INITIAL_RECV_DELAY;
  qmi_ctx->tx_endpoint_delay = ETHMAC_INITIAL_TRANSMIT_DELAY;
  // Reset by selecting default interface followed by data interface. We can't
  // start queueing transactions until this is complete.
  usb_set_interface(&usb, 8, 0);

  // set up interrupt
  status = usb_request_alloc(&int_buf, intr_max_packet, intr_addr, req_size);
  qmi_ctx->max_packet_size = bulk_max_packet;
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to allocate for usb request: %s\n",
           zx_status_get_string(status));
    goto fail;
  }
  qmi_ctx->int_txn_buf = int_buf;

  // create port to watch for interrupts and channel messages
  status = zx_port_create(0, &qmi_ctx->channel_port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create a port: %s\n",
           zx_status_get_string(status));
    goto fail;
  }
  qmi_ctx->tx_endpoint_addr = bulk_out_addr;
  qmi_ctx->rx_endpoint_addr = bulk_in_addr;
  qmi_ctx->endpoint_size = bulk_max_packet;

  // Allocate tx transaction buffers
  // TODO uint16_t tx_buf_sz = qmi_ctx->mtu;
  uint16_t tx_buf_sz = 1024;
  size_t tx_buf_remain = MAX_TX_BUF_SZ;
  while (tx_buf_remain >= tx_buf_sz) {
    usb_request_t* tx_buf;
    zx_status_t alloc_result = usb_request_alloc(
        &tx_buf, tx_buf_sz, qmi_ctx->tx_endpoint_addr, req_size);
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      goto fail;
    }

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify
    // the end of transmission when the endpoint max packet size is a factor of
    // the total transmission size
    tx_buf->header.send_zlp = true;

    zx_status_t status = usb_req_list_add_head(&qmi_ctx->tx_txn_bufs, tx_buf,
                                               qmi_ctx->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    tx_buf_remain -= tx_buf_sz;
  }

  // Allocate rx transaction buffers
  // TODO(bwb) get correct buffer sizes from usb
  uint16_t rx_buf_sz = 1024;
  size_t rx_buf_remain = MAX_RX_BUF_SZ;
  while (rx_buf_remain >= rx_buf_sz) {
    usb_request_t* rx_buf;
    zx_status_t alloc_result = usb_request_alloc(
        &rx_buf, rx_buf_sz, qmi_ctx->rx_endpoint_addr, req_size);
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      goto fail;
    }

    usb_request_complete_t complete = {
        .callback = usb_read_complete,
        .ctx = qmi_ctx,
    };
    usb_request_queue(&qmi_ctx->usb, rx_buf, &complete);
    rx_buf_remain -= rx_buf_sz;
  }

  // Set MAC addr
  qmi_ctx->mac_addr[0] = 0x62;
  qmi_ctx->mac_addr[1] = 0x77;
  qmi_ctx->mac_addr[2] = 0x62;
  qmi_ctx->mac_addr[3] = 0x62;
  qmi_ctx->mac_addr[4] = 0x77;
  qmi_ctx->mac_addr[5] = 0x62;

  // Kick off the handler thread
  int thread_result =
      thrd_create_with_name(&qmi_ctx->int_thread, qmi_transport_thread, qmi_ctx,
                            "qmi_transport_thread");
  if (thread_result != thrd_success) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create transport thread (%d)\n",
           thread_result);
    goto fail;
  }

  device_add_args_t eth_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "qmi-cdc-ethernet",
      .ctx = qmi_ctx,
      // sibling of qmi transport. Cleanup happens when qmi device unbinds
      .ops = &eth_qmi_ops,
      .proto_id = ZX_PROTOCOL_ETHMAC,
      .proto_ops = &ethmac_ops,
  };
  result = device_add(device, &eth_args, &qmi_ctx->eth_zxdev);
  if (result < 0) {
    zxlogf(ERROR, "qmi-usb-transport: failed to add ethernet device: %d\n",
           (int)result);
    goto fail;
  }

  // Add the devices
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "qmi-usb-transport",
      .ctx = qmi_ctx,
      .ops = &qmi_ops,
      .proto_id = ZX_PROTOCOL_QMI_TRANSPORT,
  };

  if ((status = device_add(device, &args, &qmi_ctx->zxdev)) < 0) {
    goto fail;
  }

  return ZX_OK;

fail:
  zxlogf(ERROR, "qmi-usb-transport: bind failed: %s\n",
         zx_status_get_string(status));
failnoerr:
  if (int_buf) {
    usb_request_release(int_buf);
  }
  free(qmi_ctx);
  return status;
}

static zx_driver_ops_t qmi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = qmi_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(qmi_usb, qmi_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, SIERRA_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, EM7565_PID),
ZIRCON_DRIVER_END(qmi_usb)
