// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qmi-usb-transport.h"
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <lib/sync/completion.h>
#include <zircon/device/qmi-transport.h>
#include <zircon/hw/usb-cdc.h>
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
} qmi_ctx_t;

static zx_status_t get_channel(void* ctx, zx_handle_t* out_channel) {
  ZX_DEBUG_ASSERT(ctx);
  zxlogf(INFO, "qmi-usb-transport: getting channel from transport\n");
  qmi_ctx_t* qmi_ctx = ctx;
  zx_status_t result = ZX_OK;

  zx_handle_t* in_channel = &qmi_ctx->channel;

  if (*in_channel != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "qmi-usb-transport: already bound, failing\n");
    result = ZX_ERR_ALREADY_BOUND;
    goto done;
  }

  result = zx_channel_create(0, in_channel, out_channel);
  if (result < 0) {
    zxlogf(ERROR, "qmi-usb-transport: Failed to create channel: %s\n",
           zx_status_get_string(result));
    goto done;
  }

done:
  return result;
}

static inline zx_status_t set_async_wait(qmi_ctx_t *ctx) {
  zx_status_t status = zx_object_wait_async(ctx->channel, ctx->channel_port, CHANNEL_MSG,
      ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
  return status;
}

static zx_status_t qmi_ioctl(void* ctx, uint32_t op, const void* in_buf,
                             size_t in_len, void* out_buf, size_t out_len,
                             size_t* out_actual) {
  qmi_ctx_t* qmi_ctx = ctx;
  zx_status_t status = ZX_OK;
  if (op != IOCTL_QMI_GET_CHANNEL) {
    status = ZX_ERR_NOT_SUPPORTED;
    goto done;
  }

  if (out_buf == NULL || out_len != sizeof(zx_handle_t)) {
    status = ZX_ERR_INVALID_ARGS;
    goto done;
  }

  zx_handle_t* out_channel = (zx_handle_t*)out_buf;
  status = get_channel(ctx, out_channel);
  if (status != ZX_OK) {
    goto done;
  }
  *out_actual = sizeof(zx_handle_t);

  status = set_async_wait(qmi_ctx);
  if (status != ZX_OK) {
    zx_handle_close(*out_channel);
    zx_handle_close(qmi_ctx->channel);
  }

done:
  zxlogf(TRACE, "qmi-usb-transport: ioctl status: %s\n",
         zx_status_get_string(status));
  return status;
}

static void qmi_release(void* ctx) {
  zxlogf(TRACE, "qmi-usb-transport: releasing device\n");
  qmi_ctx_t* qmi_ctx = ctx;
  free(qmi_ctx);
}

static void qmi_unbind(void* ctx) {
  qmi_ctx_t* qmi_ctx = ctx;
  zx_status_t result = ZX_OK;

  zxlogf(TRACE, "qmi-usb-transport: unbinding device\n");
  result = device_remove(qmi_ctx->zxdev);
  if (result != ZX_OK) {
    zxlogf(
        ERROR,
        "Failed to unbind qmi-usb-transport driver. Cannot remove device: %u\n",
        result);
  }
}

static zx_protocol_device_t qmi_ops = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = qmi_ioctl,
    .release = qmi_release,
    .unbind = qmi_unbind,
};

static void qmi_handle_interrupt(qmi_ctx_t* qmi_ctx, usb_request_t* request) {
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(ERROR, "qmi-usb-transport: ignored interrupt (size = %ld)\n",
           (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req;
  usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);

  zxlogf(TRACE, "qmi-usb-transport: Notification Available\n");
  uint16_t packet_size = qmi_ctx->max_packet_size;

  if (packet_size > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d\n", packet_size);
    return;
  }

  uint8_t buffer[packet_size];
  zx_status_t status;
  switch (usb_req.bNotification) {
    case USB_CDC_NC_RESPONSE_AVAILABLE:
      status = usb_control(
          &qmi_ctx->usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          USB_CDC_GET_ENCAPSULATED_RESPONSE, 0, QMI_INTERFACE_NUM, buffer,
          packet_size, ZX_TIME_INFINITE, NULL);
      if (!qmi_ctx->channel) {
        zxlogf(WARN, "qmi-usb-transport: recieving USB CDC frames without a channel\n");
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
    case USB_CDC_NC_NETWORK_CONNECTION:
      zxlogf(INFO, "qmi-usb-transport: Network Status: %d\n", usb_req.wValue);
      return;
    default:
      zxlogf(WARN, "qmi-usb-transport: Unknown Notification Type: %d\n",
             usb_req.bNotification);
  }
}

static void qmi_interrupt_cb(usb_request_t* req, void* cookie) {
  qmi_ctx_t* qmi_ctx = (qmi_ctx_t*)cookie;

  zxlogf(TRACE, "qmi-usb-transport: Interupt callback called!\n");
  zx_port_packet_t packet = {};
  packet.key = INTERRUPT_MSG;
  zx_port_queue(qmi_ctx->channel_port, &packet);
}

static int qmi_transport_thread(void* cookie) {
  qmi_ctx_t* ctx = cookie;
  usb_request_t* txn = ctx->int_txn_buf;

  usb_request_queue(&ctx->usb, txn);
  if (ctx->max_packet_size > 2048) {
    zxlogf(ERROR, "qmi-usb-transport: packet too big: %d\n", ctx->max_packet_size);
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
        status = zx_channel_read(ctx->channel, 0, buffer, NULL, sizeof(buffer),
                                 0, &length, NULL);
        if (status != ZX_OK) {
          zxlogf(ERROR, "qmi-usb-transport: failed to read channel: %s\n",
                 zx_status_get_string(status));
          return status;
        }
        status = usb_control(&ctx->usb,
                             USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                             USB_CDC_SEND_ENCAPSULATED_COMMAND, 0, 8, buffer,
                             length, ZX_TIME_INFINITE, NULL);
        if (status != ZX_OK) {
          zxlogf(ERROR,
                 "qmi-usb-transport: got an bad status from usb_control: %s\n",
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
          usb_request_queue(&ctx->usb, txn);
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

  qmi_ctx->parent_req_size = usb_get_request_size(&usb);
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
    zxlogf(ERROR, "qmi-usb-transport: failed to find reasonable max packet sizes");
    goto fail;
  }

  // set up interrupt
  status = usb_request_alloc(&int_buf, intr_max_packet, intr_addr, qmi_ctx->parent_req_size);
  qmi_ctx->max_packet_size = bulk_max_packet;
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to allocate for usb request: %s\n",
           zx_status_get_string(status));
    goto fail;
  }
  int_buf->complete_cb = qmi_interrupt_cb;
  int_buf->cookie = qmi_ctx;
  qmi_ctx->int_txn_buf = int_buf;

  // create port to watch for interrupts and channel messages
  status = zx_port_create(0, &qmi_ctx->channel_port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create a port: %s\n",
           zx_status_get_string(status));
    goto fail;
  }

  // Kick off the handler thread
  int thread_result =
      thrd_create_with_name(&qmi_ctx->int_thread, qmi_transport_thread, qmi_ctx,
                            "qmi_transport_thread");
  if (thread_result != thrd_success) {
    zxlogf(ERROR, "qmi-usb-transport: failed to create transport thread (%d)\n",
           thread_result);
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

  // TODO(NET-1625): set up ethernet device

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
