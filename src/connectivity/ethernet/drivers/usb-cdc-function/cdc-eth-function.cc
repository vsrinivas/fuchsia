// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/errors.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb/function.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <inet6/inet6.h>
#include <usb/usb-request.h>

namespace usb_cdc_function {

#define BULK_REQ_SIZE 2048
#define BULK_TX_COUNT 16
#define BULK_RX_COUNT 16
#define INTR_COUNT 8

#define BULK_MAX_PACKET 512  // FIXME(voydanoff) USB 3.0 support
#define INTR_MAX_PACKET sizeof(usb_cdc_speed_change_notification_t)

typedef struct {
  zx_device_t* zxdev = nullptr;
  usb_function_protocol_t function = {};

  list_node_t bulk_out_reqs = {};     // list of usb_request_t
  list_node_t bulk_in_reqs = {};      // list of usb_request_t
  list_node_t intr_reqs = {};         // list of usb_request_t
  list_node_t tx_pending_infos = {};  // list of ethernet_netbuf_t
  bool unbound = false;               // set to true when device is going away. Guarded by tx_mutex

  // Device attributes
  uint8_t mac_addr[ETH_MAC_SIZE] = {};
  // Ethernet lock -- must be acquired after tx_mutex
  // when both locks are held.
  mtx_t ethernet_mutex = {};
  ethernet_ifc_protocol_t ethernet_ifc = {};
  bool online = false;
  usb_speed_t speed = 0;
  // TX lock -- Must be acquired before ethernet_mutex
  // when both locks are held.
  mtx_t tx_mutex = {};
  mtx_t rx_mutex = {};
  mtx_t intr_mutex = {};

  uint8_t bulk_out_addr = 0;
  uint8_t bulk_in_addr = 0;
  uint8_t intr_addr = 0;
  uint16_t bulk_max_packet = 0;

  size_t parent_req_size = 0;
  mtx_t pending_request_lock = {};
  cnd_t pending_requests_completed = {};
  std::atomic_int32_t pending_request_count;
  size_t usb_request_offset = 0;
} usb_cdc_t;

typedef struct txn_info {
  ethernet_netbuf_t netbuf;
  ethernet_impl_queue_tx_callback completion_cb;
  void* cookie;
  list_node_t node;
} txn_info_t;

static void complete_txn(txn_info_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->netbuf);
}

static struct {
  usb_interface_descriptor_t comm_intf;
  usb_cs_header_interface_descriptor_t cdc_header;
  usb_cs_union_interface_descriptor_1_t cdc_union;
  usb_cs_ethernet_interface_descriptor_t cdc_eth;
  usb_endpoint_descriptor_t intr_ep;
  usb_interface_descriptor_t cdc_intf_0;
  usb_interface_descriptor_t cdc_intf_1;
  usb_endpoint_descriptor_t bulk_out_ep;
  usb_endpoint_descriptor_t bulk_in_ep;
} descriptors = {
    .comm_intf =
        {
            .bLength = sizeof(usb_interface_descriptor_t),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,  // set later
            .bAlternateSetting = 0,
            .bNumEndpoints = 1,
            .bInterfaceClass = USB_CLASS_COMM,
            .bInterfaceSubClass = USB_CDC_SUBCLASS_ETHERNET,
            .bInterfaceProtocol = 0,
            .iInterface = 0,
        },
    .cdc_header =
        {
            .bLength = sizeof(usb_cs_header_interface_descriptor_t),
            .bDescriptorType = USB_DT_CS_INTERFACE,
            .bDescriptorSubType = USB_CDC_DST_HEADER,
            .bcdCDC = 0x120,
        },
    .cdc_union =
        {
            .bLength = sizeof(usb_cs_union_interface_descriptor_1_t),
            .bDescriptorType = USB_DT_CS_INTERFACE,
            .bDescriptorSubType = USB_CDC_DST_UNION,
            .bControlInterface = 0,      // set later
            .bSubordinateInterface = 0,  // set later
        },
    .cdc_eth =
        {
            .bLength = sizeof(usb_cs_ethernet_interface_descriptor_t),
            .bDescriptorType = USB_DT_CS_INTERFACE,
            .bDescriptorSubType = USB_CDC_DST_ETHERNET,
            .iMACAddress = 0,  // set later
            .bmEthernetStatistics = 0,
            .wMaxSegmentSize = ETH_MTU,
            .wNumberMCFilters = 0,
            .bNumberPowerFilters = 0,
        },
    .intr_ep =
        {
            .bLength = sizeof(usb_endpoint_descriptor_t),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 0,  // set later
            .bmAttributes = USB_ENDPOINT_INTERRUPT,
            .wMaxPacketSize = htole16(INTR_MAX_PACKET),
            .bInterval = 8,
        },
    .cdc_intf_0 =
        {
            .bLength = sizeof(usb_interface_descriptor_t),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,  // set later
            .bAlternateSetting = 0,
            .bNumEndpoints = 0,
            .bInterfaceClass = USB_CLASS_CDC,
            .bInterfaceSubClass = 0,
            .bInterfaceProtocol = 0,
            .iInterface = 0,
        },
    .cdc_intf_1 =
        {
            .bLength = sizeof(usb_interface_descriptor_t),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,  // set later
            .bAlternateSetting = 1,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_CDC,
            .bInterfaceSubClass = 0,
            .bInterfaceProtocol = 0,
            .iInterface = 0,
        },
    .bulk_out_ep =
        {
            .bLength = sizeof(usb_endpoint_descriptor_t),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 0,  // set later
            .bmAttributes = USB_ENDPOINT_BULK,
            .wMaxPacketSize = htole16(BULK_MAX_PACKET),
            .bInterval = 0,
        },
    .bulk_in_ep =
        {
            .bLength = sizeof(usb_endpoint_descriptor_t),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 0,  // set later
            .bmAttributes = USB_ENDPOINT_BULK,
            .wMaxPacketSize = htole16(BULK_MAX_PACKET),
            .bInterval = 0,
        },
};

static void cdc_tx_complete(void* ctx, usb_request_t* req);

static void usb_request_callback(void* ctx, usb_request_t* req) {
  usb_cdc_t* cdc = static_cast<usb_cdc_t*>(ctx);
  // Invoke the real completion if not shutting down.
  if (!cdc->unbound) {
    usb_request_complete_t completion;
    memcpy(&completion, reinterpret_cast<unsigned char*>(req) + cdc->usb_request_offset,
           sizeof(completion));
    completion.callback(completion.ctx, req);
  }
  int value = --cdc->pending_request_count;
  if (value == 0) {
    mtx_lock(&cdc->pending_request_lock);
    cnd_signal(&cdc->pending_requests_completed);
    mtx_unlock(&cdc->pending_request_lock);
  }
}

static void usb_request_queue(void* ctx, usb_function_protocol_t* function, usb_request_t* req,
                              const usb_request_complete_t* completion) {
  usb_cdc_t* cdc = static_cast<usb_cdc_t*>(ctx);
  mtx_lock(&cdc->pending_request_lock);
  if (cdc->unbound) {
    mtx_unlock(&cdc->pending_request_lock);
    return;
  }
  cdc->pending_request_count++;
  mtx_unlock(&cdc->pending_request_lock);
  usb_request_complete_t internal_completion;
  internal_completion.callback = usb_request_callback;
  internal_completion.ctx = ctx;
  memcpy(reinterpret_cast<unsigned char*>(req) + cdc->usb_request_offset, completion,
         sizeof(*completion));
  usb_function_request_queue(function, req, &internal_completion);
}

static zx_status_t cdc_generate_mac_address(zx_device_t* parent, usb_cdc_t* cdc) {
  size_t actual;
  auto status = device_get_metadata(parent, DEVICE_METADATA_MAC_ADDRESS, &cdc->mac_addr,
                                    sizeof(cdc->mac_addr), &actual);
  if (status != ZX_OK || actual != sizeof(cdc->mac_addr)) {
    zxlogf(WARN, "CDC: MAC address metadata not found. Generating random address\n");

    zx_cprng_draw(cdc->mac_addr, sizeof(cdc->mac_addr));
    cdc->mac_addr[0] = 0x02;
  }

  char buffer[sizeof(cdc->mac_addr) * 3];
  snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X%02X%02X", cdc->mac_addr[0], cdc->mac_addr[1],
           cdc->mac_addr[2], cdc->mac_addr[3], cdc->mac_addr[4], cdc->mac_addr[5]);

  // Make the host and device addresses different so packets are routed correctly.
  cdc->mac_addr[5] ^= 1;

  return usb_function_alloc_string_desc(&cdc->function, buffer, &descriptors.cdc_eth.iMACAddress);
}

static zx_status_t cdc_ethernet_impl_query(void* ctx, uint32_t options, ethernet_info_t* info) {
  zxlogf(TRACE, "%s:\n", __func__);
  auto* cdc = static_cast<usb_cdc_t*>(ctx);

  // No options are supported
  if (options) {
    zxlogf(ERROR, "%s: unexpected options (0x%" PRIx32 ") to ethernet_impl_query\n", __func__,
           options);
    return ZX_ERR_INVALID_ARGS;
  }

  memset(info, 0, sizeof(*info));
  info->mtu = ETH_MTU;
  memcpy(info->mac, cdc->mac_addr, sizeof(cdc->mac_addr));
  info->netbuf_size = sizeof(txn_info_t);

  return ZX_OK;
}

static void cdc_ethernet_impl_stop(void* cookie) {
  zxlogf(TRACE, "%s:\n", __func__);
  auto* cdc = static_cast<usb_cdc_t*>(cookie);
  mtx_lock(&cdc->tx_mutex);
  mtx_lock(&cdc->ethernet_mutex);
  cdc->ethernet_ifc.ops = NULL;
  mtx_unlock(&cdc->ethernet_mutex);
  mtx_unlock(&cdc->tx_mutex);
}

static zx_status_t cdc_ethernet_impl_start(void* ctx_cookie, const ethernet_ifc_protocol_t* ifc) {
  zxlogf(TRACE, "%s:\n", __func__);
  auto* cdc = static_cast<usb_cdc_t*>(ctx_cookie);
  zx_status_t status = ZX_OK;
  if (cdc->unbound) {
    return ZX_ERR_BAD_STATE;
  }
  mtx_lock(&cdc->ethernet_mutex);
  if (cdc->ethernet_ifc.ops) {
    status = ZX_ERR_ALREADY_BOUND;
  } else {
    cdc->ethernet_ifc = *ifc;
    ethernet_ifc_status(&cdc->ethernet_ifc, cdc->online ? ETHERNET_STATUS_ONLINE : 0);
  }
  mtx_unlock(&cdc->ethernet_mutex);

  return status;
}

static zx_status_t cdc_send_locked(usb_cdc_t* cdc, ethernet_netbuf_t* netbuf) {
  if (!cdc->ethernet_ifc.ops) {
    return ZX_ERR_BAD_STATE;
  }
  const auto* byte_data = static_cast<const uint8_t*>(netbuf->data_buffer);
  size_t length = netbuf->data_size;

  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* tx_req = usb_req_list_remove_head(&cdc->bulk_in_reqs, cdc->parent_req_size);
  if (tx_req == NULL) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Send data
  tx_req->header.length = length;
  ssize_t bytes_copied = usb_request_copy_to(tx_req, byte_data, tx_req->header.length, 0);
  if (bytes_copied < 0) {
    zxlogf(LERROR, "%s: failed to copy data into send req (error %zd)\n", __func__, bytes_copied);
    zx_status_t status = usb_req_list_add_tail(&cdc->bulk_in_reqs, tx_req, cdc->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    return ZX_ERR_INTERNAL;
  }

  usb_request_complete_t complete = {
      .callback = cdc_tx_complete,
      .ctx = cdc,
  };
  usb_request_queue(cdc, &cdc->function, tx_req, &complete);

  return ZX_OK;
}

static void cdc_ethernet_impl_queue_tx(void* context, uint32_t options, ethernet_netbuf_t* netbuf,
                                       ethernet_impl_queue_tx_callback completion_cb,
                                       void* cookie) {
  auto* cdc = static_cast<usb_cdc_t*>(context);
  size_t length = netbuf->data_size;
  zx_status_t status;

  txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

  if (!cdc->online || length > ETH_MTU || length == 0 || cdc->unbound) {
    complete_txn(txn, ZX_ERR_INVALID_ARGS);
    return;
  }

  zxlogf(LTRACE, "%s: sending %zu bytes\n", __func__, length);

  mtx_lock(&cdc->tx_mutex);
  if (cdc->unbound) {
    status = ZX_ERR_IO_NOT_PRESENT;
  } else {
    status = cdc_send_locked(cdc, netbuf);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // No buffers available, queue it up
      txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
      list_add_tail(&cdc->tx_pending_infos, &txn->node);
    }
  }

  mtx_unlock(&cdc->tx_mutex);
  if (status != ZX_ERR_SHOULD_WAIT) {
    complete_txn(txn, status);
  }
}

static zx_status_t cdc_ethernet_impl_set_param(void* cookie, uint32_t param, int32_t value,
                                               const void* data, size_t data_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

static ethernet_impl_protocol_ops_t ethernet_impl_ops = []() {
  ethernet_impl_protocol_ops_t ops = {};
  ops.query = cdc_ethernet_impl_query;
  ops.stop = cdc_ethernet_impl_stop;
  ops.start = cdc_ethernet_impl_start;
  ops.queue_tx = cdc_ethernet_impl_queue_tx;
  ops.set_param = cdc_ethernet_impl_set_param;
  return ops;
}();

static void cdc_intr_complete(void* ctx, usb_request_t* req) {
  auto* cdc = static_cast<usb_cdc_t*>(ctx);

  zxlogf(LTRACE, "%s %d %ld\n", __func__, req->response.status, req->response.actual);

  mtx_lock(&cdc->intr_mutex);
  zx_status_t status = usb_req_list_add_tail(&cdc->intr_reqs, req, cdc->parent_req_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  mtx_unlock(&cdc->intr_mutex);
}

static void cdc_send_notifications(usb_cdc_t* cdc) {
  usb_request_t* req;

  usb_cdc_notification_t network_notification = {
      .bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
      .bNotification = USB_CDC_NC_NETWORK_CONNECTION,
      .wValue = cdc->online,
      .wIndex = descriptors.cdc_intf_0.bInterfaceNumber,
      .wLength = 0,
  };

  usb_cdc_speed_change_notification_t speed_notification = {
      .notification =
          {
              .bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
              .bNotification = USB_CDC_NC_CONNECTION_SPEED_CHANGE,
              .wValue = 0,
              .wIndex = descriptors.cdc_intf_0.bInterfaceNumber,
              .wLength = 2 * sizeof(uint32_t),
          },
      .downlink_br = 0,
      .uplink_br = 0,
  };

  if (cdc->online) {
    if (cdc->speed == USB_SPEED_SUPER) {
      // Claim to be gigabit speed.
      speed_notification.downlink_br = speed_notification.uplink_br = 1000 * 1000 * 1000;
    } else {
      // Claim to be 100 megabit speed.
      speed_notification.downlink_br = speed_notification.uplink_br = 100 * 1000 * 1000;
    }
  } else {
    speed_notification.downlink_br = speed_notification.uplink_br = 0;
  }

  mtx_lock(&cdc->intr_mutex);
  req = usb_req_list_remove_head(&cdc->intr_reqs, cdc->parent_req_size);
  mtx_unlock(&cdc->intr_mutex);
  if (!req) {
    zxlogf(ERROR, "%s: no interrupt request available\n", __func__);
    return;
  }

  usb_request_copy_to(req, &network_notification, sizeof(network_notification), 0);
  req->header.length = sizeof(network_notification);

  usb_request_complete_t complete = {
      .callback = cdc_intr_complete,
      .ctx = cdc,
  };
  usb_request_queue(cdc, &cdc->function, req, &complete);

  mtx_lock(&cdc->intr_mutex);
  req = usb_req_list_remove_head(&cdc->intr_reqs, cdc->parent_req_size);
  mtx_unlock(&cdc->intr_mutex);
  if (!req) {
    zxlogf(ERROR, "%s: no interrupt request available\n", __func__);
    return;
  }

  usb_request_copy_to(req, &speed_notification, sizeof(speed_notification), 0);
  req->header.length = sizeof(speed_notification);

  usb_request_queue(cdc, &cdc->function, req, &complete);
}

static void cdc_rx_complete(void* ctx, usb_request_t* req) {
  auto* cdc = static_cast<usb_cdc_t*>(ctx);

  zxlogf(LTRACE, "%s %d %ld\n", __func__, req->response.status, req->response.actual);

  if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
    mtx_lock(&cdc->rx_mutex);
    zx_status_t status = usb_req_list_add_head(&cdc->bulk_out_reqs, req, cdc->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    mtx_unlock(&cdc->rx_mutex);
    return;
  }
  if (req->response.status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_read_complete called with status %d\n", __func__, req->response.status);
  }

  if (req->response.status == ZX_OK) {
    mtx_lock(&cdc->ethernet_mutex);
    if (cdc->ethernet_ifc.ops) {
      void* data = NULL;
      usb_request_mmap(req, &data);
      ethernet_ifc_recv(&cdc->ethernet_ifc, data, req->response.actual, 0);
    }
    mtx_unlock(&cdc->ethernet_mutex);
  }

  usb_request_complete_t complete = {
      .callback = cdc_rx_complete,
      .ctx = cdc,
  };
  usb_request_queue(cdc, &cdc->function, req, &complete);
}

static void cdc_tx_complete(void* ctx, usb_request_t* req) {
  auto* cdc = static_cast<usb_cdc_t*>(ctx);

  zxlogf(LTRACE, "%s %d %ld\n", __func__, req->response.status, req->response.actual);
  if (cdc->unbound) {
    return;
  }
  mtx_lock(&cdc->tx_mutex);
  zx_status_t status = usb_req_list_add_tail(&cdc->bulk_in_reqs, req, cdc->parent_req_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  bool additional_tx_queued = false;
  txn_info_t* txn;
  zx_status_t send_status = ZX_OK;
  if ((txn = list_peek_head_type(&cdc->tx_pending_infos, txn_info_t, node))) {
    if ((send_status = cdc_send_locked(cdc, &txn->netbuf)) != ZX_ERR_SHOULD_WAIT) {
      list_remove_head(&cdc->tx_pending_infos);
      additional_tx_queued = true;
    }
  }
  mtx_unlock(&cdc->tx_mutex);

  if (additional_tx_queued) {
    mtx_lock(&cdc->ethernet_mutex);
    complete_txn(txn, send_status);
    mtx_unlock(&cdc->ethernet_mutex);
  }
}

static size_t cdc_get_descriptors_size(void* ctx) { return sizeof(descriptors); }

static void cdc_get_descriptors(void* ctx, void* buffer, size_t buffer_size, size_t* out_actual) {
  const size_t length = fbl::min(sizeof(descriptors), buffer_size);
  memcpy(buffer, &descriptors, length);
  *out_actual = length;
}

static zx_status_t cdc_control(void* ctx, const usb_setup_t* setup, const void* write_buffer,
                               size_t write_size, void* out_read_buffer, size_t read_size,
                               size_t* out_read_actual) {
  if (out_read_actual != NULL) {
    *out_read_actual = 0;
  }

  zxlogf(TRACE, "%s\n", __func__);

  // USB_CDC_SET_ETHERNET_PACKET_FILTER is the only control request required by the spec
  if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
      setup->bRequest == USB_CDC_SET_ETHERNET_PACKET_FILTER) {
    zxlogf(TRACE, "%s: USB_CDC_SET_ETHERNET_PACKET_FILTER\n", __func__);
    // TODO(voydanoff) implement the requested packet filtering
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t cdc_set_configured(void* ctx, bool configured, usb_speed_t speed) {
  zxlogf(TRACE, "%s: %d %d\n", __func__, configured, speed);
  auto* cdc = static_cast<usb_cdc_t*>(ctx);
  zx_status_t status;

  mtx_lock(&cdc->ethernet_mutex);
  cdc->online = false;
  if (cdc->ethernet_ifc.ops) {
    ethernet_ifc_status(&cdc->ethernet_ifc, 0);
  }
  mtx_unlock(&cdc->ethernet_mutex);

  if (configured) {
    if ((status = usb_function_config_ep(&cdc->function, &descriptors.intr_ep, NULL)) != ZX_OK) {
      zxlogf(ERROR, "%s: usb_function_config_ep failed\n", __func__);
      return status;
    }
    cdc->speed = speed;
  } else {
    usb_function_disable_ep(&cdc->function, cdc->bulk_out_addr);
    usb_function_disable_ep(&cdc->function, cdc->bulk_in_addr);
    usb_function_disable_ep(&cdc->function, cdc->intr_addr);
    cdc->speed = USB_SPEED_UNDEFINED;
  }

  cdc_send_notifications(cdc);

  return ZX_OK;
}

static zx_status_t cdc_set_interface(void* ctx, uint8_t interface, uint8_t alt_setting) {
  zxlogf(TRACE, "%s: %d %d\n", __func__, interface, alt_setting);
  auto* cdc = static_cast<usb_cdc_t*>(ctx);
  zx_status_t status;

  if (interface != descriptors.cdc_intf_0.bInterfaceNumber || alt_setting > 1) {
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(voydanoff) fullspeed and superspeed support
  if (alt_setting) {
    if ((status = usb_function_config_ep(&cdc->function, &descriptors.bulk_out_ep, NULL)) !=
            ZX_OK ||
        (status = usb_function_config_ep(&cdc->function, &descriptors.bulk_in_ep, NULL)) != ZX_OK) {
      zxlogf(ERROR, "%s: usb_function_config_ep failed\n", __func__);
    }
  } else {
    if ((status = usb_function_disable_ep(&cdc->function, cdc->bulk_out_addr)) != ZX_OK ||
        (status = usb_function_disable_ep(&cdc->function, cdc->bulk_in_addr)) != ZX_OK) {
      zxlogf(ERROR, "%s: usb_function_disable_ep failed\n", __func__);
    }
  }

  bool online = false;
  if (alt_setting && status == ZX_OK) {
    online = true;

    // queue our OUT reqs
    mtx_lock(&cdc->rx_mutex);
    usb_request_t* req;
    while ((req = usb_req_list_remove_head(&cdc->bulk_out_reqs, cdc->parent_req_size)) != NULL) {
      usb_request_complete_t complete = {
          .callback = cdc_rx_complete,
          .ctx = cdc,
      };
      usb_request_queue(cdc, &cdc->function, req, &complete);
    }
    mtx_unlock(&cdc->rx_mutex);
  }

  mtx_lock(&cdc->ethernet_mutex);
  cdc->online = online;
  if (cdc->ethernet_ifc.ops) {
    ethernet_ifc_status(&cdc->ethernet_ifc, online ? ETHERNET_STATUS_ONLINE : 0);
  }
  mtx_unlock(&cdc->ethernet_mutex);

  // send status notifications on interrupt endpoint
  cdc_send_notifications(cdc);

  return status;
}

usb_function_interface_protocol_ops_t device_ops = {
    .get_descriptors_size = cdc_get_descriptors_size,
    .get_descriptors = cdc_get_descriptors,
    .control = cdc_control,
    .set_configured = cdc_set_configured,
    .set_interface = cdc_set_interface,
};

static void usb_cdc_unbind(void* ctx) {
  zxlogf(TRACE, "%s\n", __func__);
  auto* cdc = static_cast<usb_cdc_t*>(ctx);
  {
    fbl::AutoLock l(&cdc->tx_mutex);
    cdc->unbound = true;
  }
  {
    fbl::AutoLock l(&cdc->pending_request_lock);
    while (cdc->pending_request_count) {
      cnd_wait(&cdc->pending_requests_completed, &cdc->pending_request_lock);
    }
  }
  {
    fbl::AutoLock l(&cdc->tx_mutex);
    txn_info_t* txn;
    while ((txn = list_remove_head_type(&cdc->tx_pending_infos, txn_info_t, node)) != NULL) {
      complete_txn(txn, ZX_ERR_PEER_CLOSED);
    }
  }
  device_unbind_reply(cdc->zxdev);
}

static void usb_cdc_release(void* ctx) {
  zxlogf(TRACE, "%s\n", __func__);
  auto* cdc = static_cast<usb_cdc_t*>(ctx);
  usb_request_t* req;

  while ((req = usb_req_list_remove_head(&cdc->bulk_out_reqs, cdc->parent_req_size)) != NULL) {
    usb_request_release(req);
  }
  while ((req = usb_req_list_remove_head(&cdc->bulk_in_reqs, cdc->parent_req_size)) != NULL) {
    usb_request_release(req);
  }
  while ((req = usb_req_list_remove_head(&cdc->intr_reqs, cdc->parent_req_size)) != NULL) {
    usb_request_release(req);
  }
  mtx_destroy(&cdc->ethernet_mutex);
  mtx_destroy(&cdc->tx_mutex);
  mtx_destroy(&cdc->rx_mutex);
  mtx_destroy(&cdc->intr_mutex);
  delete cdc;
}

static zx_protocol_device_t usb_cdc_proto = []() {
  zx_protocol_device_t dev = {};
  dev.version = DEVICE_OPS_VERSION;
  dev.unbind = usb_cdc_unbind;
  dev.release = usb_cdc_release;
  return dev;
}();

zx_status_t usb_cdc_bind(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "%s\n", __func__);
  device_add_args_t args = {};

  auto cdc = std::make_unique<usb_cdc_t>();
  if (!cdc) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB_FUNCTION, &cdc->function);
  if (status != ZX_OK) {
    return status;
  }
  cnd_init(&cdc->pending_requests_completed);
  list_initialize(&cdc->bulk_out_reqs);
  list_initialize(&cdc->bulk_in_reqs);
  list_initialize(&cdc->intr_reqs);
  list_initialize(&cdc->tx_pending_infos);
  mtx_init(&cdc->ethernet_mutex, mtx_plain);
  mtx_init(&cdc->tx_mutex, mtx_plain);
  mtx_init(&cdc->rx_mutex, mtx_plain);
  mtx_init(&cdc->intr_mutex, mtx_plain);

  cdc->bulk_max_packet = BULK_MAX_PACKET;  // FIXME(voydanoff) USB 3.0 support
  cdc->parent_req_size = usb_function_get_request_size(&cdc->function);
  uint64_t req_size =
      cdc->parent_req_size + sizeof(usb_req_internal_t) + sizeof(usb_request_complete_t);
  cdc->usb_request_offset = cdc->parent_req_size + sizeof(usb_req_internal_t);
  status = usb_function_alloc_interface(&cdc->function, &descriptors.comm_intf.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_function_alloc_interface failed\n", __func__);
    goto fail;
  }
  status = usb_function_alloc_interface(&cdc->function, &descriptors.cdc_intf_0.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_function_alloc_interface failed\n", __func__);
    goto fail;
  }
  descriptors.cdc_intf_1.bInterfaceNumber = descriptors.cdc_intf_0.bInterfaceNumber;
  descriptors.cdc_union.bControlInterface = descriptors.comm_intf.bInterfaceNumber;
  descriptors.cdc_union.bSubordinateInterface = descriptors.cdc_intf_0.bInterfaceNumber;

  status = usb_function_alloc_ep(&cdc->function, USB_DIR_OUT, &cdc->bulk_out_addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __func__);
    goto fail;
  }
  status = usb_function_alloc_ep(&cdc->function, USB_DIR_IN, &cdc->bulk_in_addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __func__);
    goto fail;
  }
  status = usb_function_alloc_ep(&cdc->function, USB_DIR_IN, &cdc->intr_addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __func__);
    goto fail;
  }

  descriptors.bulk_out_ep.bEndpointAddress = cdc->bulk_out_addr;
  descriptors.bulk_in_ep.bEndpointAddress = cdc->bulk_in_addr;
  descriptors.intr_ep.bEndpointAddress = cdc->intr_addr;

  status = cdc_generate_mac_address(parent, cdc.get());
  if (status != ZX_OK) {
    goto fail;
  }

  // allocate bulk out usb requests
  usb_request_t* req;
  for (int i = 0; i < BULK_TX_COUNT; i++) {
    status = usb_request_alloc(&req, BULK_REQ_SIZE, cdc->bulk_out_addr, req_size);
    if (status != ZX_OK) {
      goto fail;
    }
    status = usb_req_list_add_head(&cdc->bulk_out_reqs, req, cdc->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
  // allocate bulk in usb requests
  for (int i = 0; i < BULK_RX_COUNT; i++) {
    status = usb_request_alloc(&req, BULK_REQ_SIZE, cdc->bulk_in_addr, req_size);
    if (status != ZX_OK) {
      goto fail;
    }

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
    // transmission when the endpoint max packet size is a factor of the total transmission size
    req->header.send_zlp = true;

    status = usb_req_list_add_head(&cdc->bulk_in_reqs, req, cdc->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  // allocate interrupt requests
  for (int i = 0; i < INTR_COUNT; i++) {
    status = usb_request_alloc(&req, INTR_MAX_PACKET, cdc->intr_addr, req_size);
    if (status != ZX_OK) {
      goto fail;
    }

    status = usb_req_list_add_head(&cdc->intr_reqs, req, cdc->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "cdc-eth-function";
  args.ctx = cdc.get();
  args.ops = &usb_cdc_proto;
  args.proto_id = ZX_PROTOCOL_ETHERNET_IMPL;
  args.proto_ops = &ethernet_impl_ops;

  status = device_add(parent, &args, &cdc->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: add_device failed %d\n", __func__, status);
    goto fail;
  }
  usb_function_set_interface(&cdc->function, cdc.get(), &device_ops);
  {
    // The DDK now owns this reference.
    __UNUSED auto released = cdc.release();
  }
  return ZX_OK;

fail:
  usb_cdc_release(cdc.get());
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = usb_cdc_bind;
  return ops;
}();

}  // namespace usb_cdc_function

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_cdc, usb_cdc_function::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_COMM),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_CDC_SUBCLASS_ETHERNET),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(usb_cdc)
