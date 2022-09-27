// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-ecm.h"

#include <lib/fit/defer.h>

#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/ethernet_usb_cdc_ecm-bind.h"

namespace {
// The maximum amount of memory we are willing to allocate to transaction buffers
constexpr size_t kMaxTxBufferSize = 32768;
constexpr size_t kMaxRxBufferSize = (1500 * 2048);
constexpr uint64_t kEthernetMaxTransmitDelay = 100;
constexpr uint64_t kEthernetMaxRecvDelay = 100;
constexpr uint64_t kEthernetTransmitDelay = 10;
constexpr uint64_t kEthernetRecvDelay = 10;
constexpr uint64_t kEthernetInitialTransmitDelay = 0;
constexpr uint64_t kEthernetInitialRecvDelay = 0;
constexpr uint16_t kEthernetInitialPacketFilter =
    (USB_CDC_PACKET_TYPE_DIRECTED | USB_CDC_PACKET_TYPE_BROADCAST | USB_CDC_PACKET_TYPE_MULTICAST);
}  // namespace

namespace usb_cdc_ecm {
void UsbCdcEcm::CompleteTxn(txn_info_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->netbuf);
}

zx_status_t UsbCdcEcm::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;
  if (proto_id == ZX_PROTOCOL_ETHERNET_IMPL) {
    proto->ops = &ethernet_impl_protocol_ops_;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void UsbCdcEcm::DdkRelease() { delete this; }

void UsbCdcEcm::DdkUnbind(ddk::UnbindTxn unbindtxn) {
  // TODO: Instead of taking the lock here and holding up the whole driver host, offload to the
  // worker thread and take ownership of the UnbindTxn.
  fbl::AutoLock tx_lock(&ecm_ctx_.tx_mutex);
  ecm_ctx_.unbound = true;
  txn_info_t* pending_txn;
  while ((pending_txn = list_remove_head_type(ecm_ctx_.tx_pending_infos(), txn_info_t, node)) !=
         nullptr) {
    CompleteTxn(pending_txn, ZX_ERR_PEER_CLOSED);
  }
  unbindtxn.Reply();
}

void UsbCdcEcm::EcmFree() {
  if (ecm_ctx_.int_thread.has_value()) {
    thrd_join(ecm_ctx_.int_thread.value(), nullptr);
  }
  list_node_t bufs;
  {
    fbl::AutoLock _(&ecm_ctx_.tx_mutex);
    list_move(ecm_ctx_.tx_txn_bufs(), &bufs);
  }
  usb_request_t* req;
  while ((req = usb_req_list_remove_head(&bufs, ecm_ctx_.parent_req_size)) != nullptr) {
    usb_request_release(req);
  }
  if (ecm_ctx_.int_txn_buf) {
    usb_request_release(ecm_ctx_.int_txn_buf);
  }
}

void UsbCdcEcm::EcmUpdateOnlineStatus(void* cookie, bool is_online) {
  auto* ctx = reinterpret_cast<EcmCtx*>(cookie);

  fbl::AutoLock ethernet_lock(&ctx->ethernet_mutex);

  if ((is_online && ctx->online) || (!is_online && !ctx->online)) {
    return;
  }
  if (is_online) {
    zxlogf(INFO, "Connected to network");
    ctx->online = true;
    if (ctx->ethernet_ifc.ops != nullptr) {
      ethernet_ifc_status(&ctx->ethernet_ifc, ETHERNET_STATUS_ONLINE);
    } else {
      zxlogf(WARNING, "Not connected to ethermac interface");
    }
  } else {
    zxlogf(INFO, "No connection to network");
    ctx->online = false;
    if (ctx->ethernet_ifc.ops != nullptr) {
      ethernet_ifc_status(&ctx->ethernet_ifc, 0);
    }
  }
}

zx_status_t UsbCdcEcm::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  zxlogf(DEBUG, "%s called", __FUNCTION__);

  // No options are supported
  if (options) {
    zxlogf(ERROR, "Unexpected options (0x%08" PRIx32 ") to EthernetImplQuery", options);
    return ZX_ERR_INVALID_ARGS;
  }

  *info = {};
  info->mtu = ecm_ctx_.mtu;
  memcpy(info->mac, ecm_ctx_.mac_addr, sizeof(ecm_ctx_.mac_addr));
  info->netbuf_size = sizeof(txn_info_t);

  return ZX_OK;
}

void UsbCdcEcm::EthernetImplStop() {
  fbl::AutoLock tx_lock(&ecm_ctx_.tx_mutex);
  fbl::AutoLock ethernet_lock(&ecm_ctx_.ethernet_mutex);
  ecm_ctx_.ethernet_ifc.ops = nullptr;
}

zx_status_t UsbCdcEcm::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  fbl::AutoLock ethernet_lock(&ecm_ctx_.ethernet_mutex);
  zx_status_t status = ZX_OK;

  if (ecm_ctx_.ethernet_ifc.ops != nullptr) {
    status = ZX_ERR_ALREADY_BOUND;
  } else {
    ecm_ctx_.ethernet_ifc = *ifc;
    ethernet_ifc_status(&ecm_ctx_.ethernet_ifc, ecm_ctx_.online ? ETHERNET_STATUS_ONLINE : 0);
  }
  return status;
}

void UsbCdcEcm::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                    ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  zxlogf(DEBUG, "%s called", __FUNCTION__);
  zx_status_t status;

  txn_info_t* txn = containerof(netbuf, txn_info_t, netbuf);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

  size_t length = netbuf->data_size;
  if (length > ecm_ctx_.mtu || length == 0) {
    CompleteTxn(txn, ZX_ERR_INVALID_ARGS);
    return;
  }

  zxlogf(TRACE, "Sending %zu bytes to endpoint 0x%08" PRIx8 "", length, ecm_ctx_.tx_endpoint.addr);
  {
    fbl::AutoLock tx_lock(&ecm_ctx_.tx_mutex);
    if (ecm_ctx_.unbound) {
      status = ZX_ERR_IO_NOT_PRESENT;
    } else {
      status = SendLocked(netbuf);
      if (status == ZX_ERR_SHOULD_WAIT) {
        // No buffers available, queue it up
        list_add_tail(ecm_ctx_.tx_pending_infos(), &txn->node);
      }
    }
  }
  if (status != ZX_ERR_SHOULD_WAIT) {
    CompleteTxn(txn, status);
  }
}
zx_status_t UsbCdcEcm::EthernetImplSetParam(uint32_t param, int32_t value, const uint8_t* data,
                                            size_t data_size) {
  zxlogf(DEBUG, "%s called", __FUNCTION__);
  zx_status_t status;

  switch (param) {
    case ETHERNET_SETPARAM_PROMISC:
      status = SetPacketFilter(USB_CDC_PACKET_TYPE_PROMISCUOUS, (bool)value);
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
  }

  return status;
}

zx_status_t UsbCdcEcm::QueueRequest(const uint8_t* data, size_t length, usb_request_t* req) {
  req->header.length = length;
  if (ecm_ctx_.ethernet_ifc.ops == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  ssize_t bytes_copied = usb_request_copy_to(req, data, length, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "Failed to copy data into send txn (error %zd)", bytes_copied);
    return ZX_ERR_IO;
  }
  usb_request_complete_callback_t complete = {
      .callback = [](void* arg, usb_request_t* request) -> void {
        static_cast<UsbCdcEcm*>(arg)->UsbWriteComplete(request);
      },
      .ctx = this,
  };
  usb_request_queue(&ecm_ctx_.usbproto, req, &complete);
  return ZX_OK;
}

void UsbCdcEcm::UsbRecv(usb_request_t* request) {
  size_t len = request->response.actual;

  uint8_t* read_data;
  zx_status_t status = usb_request_mmap(request, reinterpret_cast<void**>(&read_data));
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_mmap failed with status %d", status);
    return;
  }
  {
    fbl::AutoLock ethernet_lock(&ecm_ctx_.ethernet_mutex);
    if (ecm_ctx_.ethernet_ifc.ops != nullptr) {
      ethernet_ifc_recv(&ecm_ctx_.ethernet_ifc, read_data, len, 0);
    }
  }
}

zx_status_t UsbCdcEcm::SetPacketFilter(uint16_t mode, bool on) {
  zx_status_t status = ZX_OK;
  uint16_t bits = ecm_ctx_.rx_packet_filter;

  if (on) {
    bits |= mode;
  } else {
    bits &= ~mode;
  }

  status =
      usb_control_out(&ecm_ctx_.usbproto, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                      USB_CDC_SET_ETHERNET_PACKET_FILTER, bits, 0, ZX_TIME_INFINITE, nullptr, 0);

  if (status != ZX_OK) {
    zxlogf(ERROR, "usb-cdc-ecm: Set packet filter failed: %d", status);
    return status;
  }
  ecm_ctx_.rx_packet_filter = bits;
  return status;
}

void UsbCdcEcm::EcmHandleInterrupt(void* cookie, usb_request_t* request) {
  EcmCtx* ctx = reinterpret_cast<EcmCtx*>(cookie);
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(DEBUG, "Ignored interrupt (size = %ld)", (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req{};
  __UNUSED size_t result =
      usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);
  if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
      usb_req.bNotification == USB_CDC_NC_NETWORK_CONNECTION) {
    EcmUpdateOnlineStatus(ctx, usb_req.wValue != 0);
  } else if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
             usb_req.bNotification == USB_CDC_NC_CONNECTION_SPEED_CHANGE) {
    // The ethermac driver doesn't care about speed changes, so even though we track this
    // information, it's currently unused.
    if (usb_req.wLength != 8) {
      zxlogf(ERROR, "Invalid size (%" PRIu16 ") for CONNECTION_SPEED_CHANGE notification",
             usb_req.wLength);
      return;
    }
    // Data immediately follows notification in packet
    uint32_t new_us_bps = 0, new_ds_bps = 0;
    result = usb_request_copy_from(request, &new_us_bps, 4, sizeof(usb_cdc_notification_t));
    result = usb_request_copy_from(request, &new_ds_bps, 4, sizeof(usb_cdc_notification_t) + 4);
    if (new_us_bps != ctx->us_bps) {
      zxlogf(INFO, "Connection speed change... upstream bits/s: %" PRIu32 "", new_us_bps);
      ctx->us_bps = new_us_bps;
    }
    if (new_ds_bps != ctx->ds_bps) {
      zxlogf(INFO, "Connection speed change... downstream bits/s: %" PRIu32 "", new_ds_bps);
      ctx->ds_bps = new_ds_bps;
    }
  } else {
    zxlogf(ERROR, "Ignored interrupt (type = %" PRIu8 ", request = %" PRIu8 ")",
           usb_req.bmRequestType, usb_req.bNotification);
    return;
  }
}

int UsbCdcEcm::EcmIntHandlerThread(void* cookie) {
  EcmCtx* ctx = reinterpret_cast<EcmCtx*>(cookie);

  usb_request_t* req = ctx->int_txn_buf;

  usb_request_complete_callback_t complete = {
      .callback = [](void* arg, usb_request_t* request) -> void {
        static_cast<EcmCtx*>(arg)->EcmInterruptComplete(arg, request);
      },
      .ctx = ctx,
  };
  while (true) {
    sync_completion_reset(&ctx->completion);
    usb_request_queue(&ctx->usbproto, req, &complete);
    sync_completion_wait(&ctx->completion, ZX_TIME_INFINITE);
    if (req->response.status == ZX_OK) {
      EcmHandleInterrupt(ctx, req);
    } else if (req->response.status == ZX_ERR_PEER_CLOSED ||
               req->response.status == ZX_ERR_IO_NOT_PRESENT) {
      zxlogf(DEBUG, "Terminating interrupt handling thread");
      return req->response.status;
    } else if (req->response.status == ZX_ERR_IO_REFUSED ||
               req->response.status == ZX_ERR_IO_INVALID) {
      zxlogf(DEBUG, "Resetting interrupt endpoint");
      usb_reset_endpoint(&ctx->usbproto, ctx->int_endpoint.addr);
    } else {
      zxlogf(ERROR, "Error (%s) waiting for interrupt - ignoring",
             zx_status_get_string(req->response.status));
    }
  }
}

void UsbCdcEcm::CopyEndpointInfo(ecm_endpoint_t* ep_info, usb_endpoint_descriptor_t* desc) {
  ep_info->addr = desc->b_endpoint_address;
  ep_info->max_packet_size = desc->w_max_packet_size;
}

void UsbCdcEcm::DdkInit(ddk::InitTxn txn) {
  zxlogf(DEBUG, "Starting %s", __FUNCTION__);
  // TODO: use usb:DescriptorList
  usb_desc_iter_t iter{};

  auto cleanup = fit::defer([&]() {
    if (iter.desc) {
      usb_desc_iter_release(&iter);
    }
    EcmFree();
    zxlogf(ERROR, "Failed to complete DdkInit");
  });

  {
    fbl::AutoLock _(&ecm_ctx_.tx_mutex);
    list_initialize(ecm_ctx_.tx_txn_bufs());
    list_initialize(ecm_ctx_.tx_pending_infos());
  }

  zx_status_t result =
      usb_control_out(&ecm_ctx_.usbproto, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                      USB_CDC_SET_ETHERNET_PACKET_FILTER, kEthernetInitialPacketFilter, 0,
                      ZX_TIME_INFINITE, nullptr, 0);
  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to set initial packet filter: %d", (int)result);
    return txn.Reply(result);
  }
  ecm_ctx_.rx_packet_filter = kEthernetInitialPacketFilter;
  ecm_ctx_.parent_req_size = usb_get_request_size(&ecm_ctx_.usbproto);

  usb_endpoint_descriptor_t* int_ep = nullptr;
  usb_endpoint_descriptor_t* tx_ep = nullptr;
  usb_endpoint_descriptor_t* rx_ep = nullptr;
  usb_interface_descriptor_t* default_ifc = nullptr;
  usb_interface_descriptor_t* data_ifc = nullptr;

  // Find the CDC descriptors and endpoints
  result = usb_desc_iter_init(&ecm_ctx_.usbproto, &iter);
  if (result != ZX_OK) {
    return txn.Reply(result);
  }
  result = ecm_ctx_.ParseUsbDescriptor(&iter, &int_ep, &tx_ep, &rx_ep, &default_ifc, &data_ifc);
  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to parse usb descriptor: %d", (int)result);
    return txn.Reply(result);
  }

  // Parse endpoint information
  CopyEndpointInfo(&ecm_ctx_.int_endpoint, int_ep);
  CopyEndpointInfo(&ecm_ctx_.tx_endpoint, tx_ep);
  CopyEndpointInfo(&ecm_ctx_.rx_endpoint, rx_ep);

  ecm_ctx_.rx_endpoint_delay = kEthernetInitialRecvDelay;
  ecm_ctx_.tx_endpoint_delay = kEthernetInitialTransmitDelay;
  // Reset by selecting default interface followed by data interface. We can't start
  // queueing transactions until this is complete.
  usb_set_interface(&ecm_ctx_.usbproto, default_ifc->b_interface_number,
                    default_ifc->b_alternate_setting);
  usb_set_interface(&ecm_ctx_.usbproto, data_ifc->b_interface_number,
                    data_ifc->b_alternate_setting);
  // Allocate interrupt transaction buffer
  uint64_t req_size = ecm_ctx_.parent_req_size + sizeof(usb_req_internal_t);

  usb_request_t* int_buf;
  zx_status_t alloc_result = usb_request_alloc(&int_buf, ecm_ctx_.int_endpoint.max_packet_size,
                                               ecm_ctx_.int_endpoint.addr, req_size);

  if (alloc_result != ZX_OK) {
    result = alloc_result;
    return txn.Reply(result);
  }
  ecm_ctx_.int_txn_buf = int_buf;

  // Allocate tx transaction buffers
  uint16_t tx_buf_sz = ecm_ctx_.mtu;
  static_assert(kMaxTxBufferSize < UINT16_MAX, "insufficient space for even a single tx buffer");
  if (tx_buf_sz > kMaxTxBufferSize) {
    zxlogf(ERROR, "Insufficient space for even a single tx buffer");
    return txn.Reply(result);
  }
  size_t tx_buf_remain = kMaxTxBufferSize;
  while (tx_buf_remain >= tx_buf_sz) {
    usb_request_t* tx_buf;
    alloc_result = usb_request_alloc(&tx_buf, tx_buf_sz, ecm_ctx_.tx_endpoint.addr, req_size);
    tx_buf->direct = true;
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      return txn.Reply(result);
    }

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
    // transmission when the endpoint max packet size is a factor of the total transmission size
    tx_buf->header.send_zlp = true;
    {
      fbl::AutoLock _(&ecm_ctx_.tx_mutex);
      zx_status_t add_result =
          usb_req_list_add_head(ecm_ctx_.tx_txn_bufs(), tx_buf, ecm_ctx_.parent_req_size);
      ZX_DEBUG_ASSERT(add_result == ZX_OK);
    }
    tx_buf_remain -= tx_buf_sz;
  }

  // Allocate rx transaction buffers
  uint32_t rx_buf_sz = ecm_ctx_.mtu;
  static_assert(kMaxRxBufferSize >= UINT16_MAX, "insufficient space for even a single rx buffer");
  if (rx_buf_sz > kMaxRxBufferSize) {
    zxlogf(ERROR, "Insufficient space for even a single rx buffer");
    return txn.Reply(result);
  }
  usb_request_complete_callback_t complete = {
      .callback = [](void* arg, usb_request_t* request) -> void {
        static_cast<UsbCdcEcm*>(arg)->UsbReadComplete(request);
      },
      .ctx = this,
  };

  size_t rx_buf_remain = kMaxRxBufferSize;
  while (rx_buf_remain >= rx_buf_sz) {
    usb_request_t* rx_buf;
    zx_status_t alloc_result =
        usb_request_alloc(&rx_buf, rx_buf_sz, ecm_ctx_.rx_endpoint.addr, req_size);
    if (alloc_result != ZX_OK) {
      result = alloc_result;
      return txn.Reply(result);
    }
    rx_buf->direct = true;
    usb_request_queue(&ecm_ctx_.usbproto, rx_buf, &complete);
    rx_buf_remain -= rx_buf_sz;
  }

  // Kick off the handler thread
  thrd_t thread;
  int thread_result = thrd_create_with_name(&thread, UsbCdcEcm::EcmIntHandlerThread, &ecm_ctx_,
                                            "EcmIntHandlerThread");
  if (thread_result != thrd_success) {
    zxlogf(ERROR, "Failed to create interrupt handler thread (%d)", thread_result);
    return txn.Reply(result);
  }
  ecm_ctx_.int_thread = thread;
  cleanup.cancel();
  return txn.Reply(ZX_OK);
}

zx_status_t UsbCdcEcm::EcmBind(void* ctx, zx_device_t* device) {
  zxlogf(DEBUG, "Starting %s", __FUNCTION__);
  auto dev = std::make_unique<UsbCdcEcm>(device);
  auto args = ddk::DeviceAddArgs("usb-cdc-ecm");
  zx_device_str_prop_t str_props[] = {
      zx_device_str_prop_t{
          .key = "fuchsia.ethernet.NETDEVICE_MIGRATION",
          .property_value = str_prop_bool_val(true),
      },
  };
  EcmCtx* ecm_ctx = &dev->ecm_ctx_;

  usb_protocol_t usb;
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (result != ZX_OK) {
    return result;
  }
  memcpy(&ecm_ctx->usbproto, &usb, sizeof(usb_protocol_t));

  // Initialize context

  usb_composite_protocol_t usb_composite;
  result = device_get_protocol(device, ZX_PROTOCOL_USB_COMPOSITE, &usb_composite);
  if (result != ZX_OK) {
    return result;
  }

  result = usb_claim_additional_interfaces(&usb_composite, WantInterface, nullptr);
  if (result != ZX_OK) {
    return result;
  }
  ecm_ctx->usb_device = device;

  bool is_dfv2 = device_is_dfv2(device);

  if (is_dfv2) {
    args.set_str_props(cpp20::span<const zx_device_str_prop_t>(str_props));
  }

  result = dev->DdkAdd(args);
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Error adding device: %s", __func__, zx_status_get_string(result));
    return result;
  }

  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  auto __UNUSED temp_ref = dev.release();
  return ZX_OK;
}

zx_status_t UsbCdcEcm::SendLocked(ethernet_netbuf_t* netbuf) {
  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* req = usb_req_list_remove_head(ecm_ctx_.tx_txn_bufs(), ecm_ctx_.parent_req_size);
  if (req == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(ecm_ctx_.tx_endpoint_delay)));
  zx_status_t status;
  const uint8_t* byte_data = netbuf->data_buffer;
  size_t length = netbuf->data_size;
  if ((status = QueueRequest(byte_data, length, req)) != ZX_OK) {
    zx_status_t add_status =
        usb_req_list_add_tail(ecm_ctx_.tx_txn_bufs(), req, ecm_ctx_.parent_req_size);
    ZX_DEBUG_ASSERT(add_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

void UsbCdcEcm::UsbReadComplete(usb_request_t* request) __TA_NO_THREAD_SAFETY_ANALYSIS {
  if (request->response.status != ZX_OK) {
    zxlogf(DEBUG, "UsbReadComplete called with status %d", (int)request->response.status);
  }

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "Resetting receive endpoint");
    request->reset = true;
    request->reset_address = ecm_ctx_.rx_endpoint.addr;
    usb_request_complete_callback_t complete = {
        .callback = [](void* arg, usb_request_t* request) -> void {
          static_cast<UsbCdcEcm*>(arg)->UsbReadComplete(request);
        },
        .ctx = this,
    };
    usb_request_queue(&ecm_ctx_.usbproto, request, &complete);
    return;
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    if (ecm_ctx_.rx_endpoint_delay < kEthernetMaxRecvDelay) {
      ecm_ctx_.rx_endpoint_delay += kEthernetRecvDelay;
    }
    zxlogf(DEBUG,
           "Slowing down the requests by %lu usec."
           "Resetting the recv endpoint\n",
           kEthernetRecvDelay);
    request->reset = true;
    request->reset_address = ecm_ctx_.rx_endpoint.addr;

    usb_request_complete_callback_t complete = {
        .callback = [](void* arg, usb_request_t* request) -> void {
          static_cast<UsbCdcEcm*>(arg)->UsbReadComplete(request);
        },
        .ctx = this,
    };

    usb_request_queue(&ecm_ctx_.usbproto, request, &complete);
    return;
  } else if (request->response.status == ZX_OK && !request->reset) {
    UsbRecv(request);
  }
  if (ecm_ctx_.rx_endpoint_delay) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(ecm_ctx_.rx_endpoint_delay)));
  }
  request->reset = false;
  usb_request_complete_callback_t complete = {
      .callback = [](void* arg, usb_request_t* request) -> void {
        static_cast<UsbCdcEcm*>(arg)->UsbReadComplete(request);
      },
      .ctx = this,
  };
  usb_request_queue(&ecm_ctx_.usbproto, request, &complete);
}

void UsbCdcEcm::UsbWriteComplete(usb_request_t* request) __TA_NO_THREAD_SAFETY_ANALYSIS {
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  // Declaring variables here as they are used across the blocks of code guarded by different mutex
  bool additional_tx_queued = false;
  txn_info_t* txn;
  zx_status_t send_status = ZX_OK;
  {
    fbl::AutoLock tx_lock(&ecm_ctx_.tx_mutex);
    // If reset, we still hold the TX mutex.
    if (!request->reset) {
      // Return transmission buffer to pool
      zx_status_t status =
          usb_req_list_add_tail(ecm_ctx_.tx_txn_bufs(), request, ecm_ctx_.parent_req_size);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(DEBUG, "Resetting transmit endpoint");
        request->reset = true;
        request->reset_address = ecm_ctx_.tx_endpoint.addr;
        usb_request_complete_callback_t complete = {
            .callback = [](void* arg, usb_request_t* request) -> void {
              static_cast<UsbCdcEcm*>(arg)->UsbWriteComplete(request);
            },
            .ctx = this,
        };
        usb_request_queue(&ecm_ctx_.usbproto, request, &complete);
        return;
      }

      if (request->response.status == ZX_ERR_IO_INVALID) {
        zxlogf(DEBUG,
               "Slowing down the requests by %lu usec."
               "Resetting the transmit endpoint\n",
               kEthernetTransmitDelay);
        if (ecm_ctx_.tx_endpoint_delay < kEthernetMaxTransmitDelay) {
          ecm_ctx_.tx_endpoint_delay += kEthernetTransmitDelay;
        }
        request->reset = true;
        request->reset_address = ecm_ctx_.tx_endpoint.addr;
        usb_request_complete_callback_t complete = {
            .callback = [](void* arg, usb_request_t* request) -> void {
              static_cast<UsbCdcEcm*>(arg)->UsbWriteComplete(request);
            },
            .ctx = this,
        };
        usb_request_queue(&ecm_ctx_.usbproto, request, &complete);
        return;
      }
    }
    request->reset = false;
    if (!list_is_empty(ecm_ctx_.tx_pending_infos())) {
      txn = list_peek_head_type(ecm_ctx_.tx_pending_infos(), txn_info_t, node);
      if ((send_status = SendLocked(&txn->netbuf)) != ZX_ERR_SHOULD_WAIT) {
        list_remove_head(ecm_ctx_.tx_pending_infos());
        additional_tx_queued = true;
      }
    }
  }

  {
    fbl::AutoLock ethernet_lock(&ecm_ctx_.ethernet_mutex);
    if (additional_tx_queued) {
      CompleteTxn(txn, send_status);
    }
  }

  // When the interface is offline, the transaction will complete with status set to
  // ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

}  // namespace usb_cdc_ecm

static constexpr zx_driver_ops_t ecm_driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = &usb_cdc_ecm::UsbCdcEcm::EcmBind;

  return ops;
}();

ZIRCON_DRIVER(ethernet_usb_cdc_ecm, ecm_driver_ops, "zircon", "0.1");
