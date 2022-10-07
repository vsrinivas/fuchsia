// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/usb-cdc-ecm.h"

#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/composite/c/banjo.h>
#include <lib/ddk/debug.h>
#include <zircon/status.h>

#include <usb/cdc.h>
#include <usb/usb-request.h>

#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/ethernet_usb_cdc_ecm-bind.h"
#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/usb-cdc-ecm-lib.h"

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

static void CompleteTxn(txn_info_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->netbuf);
}

static bool WantInterface(usb_interface_descriptor_t* intf, void* arg) {
  return intf->b_interface_class == USB_CLASS_CDC;
}

void UsbCdcEcm::DdkUnbind(ddk::UnbindTxn unbind_txn) {
  // TODO: Instead of taking the lock here and holding up the whole driver host, offload to the
  // worker thread and take ownership of the UnbindTxn.
  fbl::AutoLock tx_lock(&tx_mutex_);
  unbound_ = true;
  txn_info_t* pending_txn;
  while ((pending_txn = list_remove_head_type(&tx_pending_infos_, txn_info_t, node)) != nullptr) {
    CompleteTxn(pending_txn, ZX_ERR_PEER_CLOSED);
  }
  unbind_txn.Reply();
}

UsbCdcEcm::~UsbCdcEcm() {
  if (int_thread_) {
    thrd_join(int_thread_, nullptr);
  }
  usb_request_t* txn;
  while ((txn = usb_req_list_remove_head(&tx_txn_bufs_, parent_req_size_)) != nullptr) {
    usb_request_release(txn);
  }
  if (int_txn_buf_) {
    usb_request_release(int_txn_buf_);
  }
}

void UsbCdcEcm::DdkRelease() { delete this; }

void UsbCdcEcm::UpdateOnlineStatus(bool is_online) {
  fbl::AutoLock ethernet_lock(&ethernet_mutex_);
  if ((is_online && online_) || (!is_online && !online_)) {
    return;
  }

  if (is_online) {
    zxlogf(INFO, "Connected to network");
    online_ = true;
    if (ethernet_ifc_.ops) {
      ethernet_ifc_status(&ethernet_ifc_, ETHERNET_STATUS_ONLINE);
    } else {
      zxlogf(WARNING, "Not connected to ethermac interface");
    }
  } else {
    zxlogf(INFO, "No connection to network");
    online_ = false;
    if (ethernet_ifc_.ops) {
      ethernet_ifc_status(&ethernet_ifc_, 0);
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
  info->mtu = mtu_;
  memcpy(info->mac, mac_addr_.data(), mac_addr_.size());
  info->netbuf_size = sizeof(txn_info_t);

  return ZX_OK;
}

void UsbCdcEcm::EthernetImplStop() {
  fbl::AutoLock tx_lock(&tx_mutex_);
  fbl::AutoLock ethernet_lock(&ethernet_mutex_);
  ethernet_ifc_.ops = nullptr;
}

zx_status_t UsbCdcEcm::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  fbl::AutoLock ethernet_lock(&ethernet_mutex_);
  zx_status_t status = ZX_OK;

  if (ethernet_ifc_.ops != nullptr) {
    status = ZX_ERR_ALREADY_BOUND;
  } else {
    ethernet_ifc_ = *ifc;
    ethernet_ifc_status(&ethernet_ifc_, online_ ? ETHERNET_STATUS_ONLINE : 0);
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
  if (length > mtu_ || length == 0) {
    CompleteTxn(txn, ZX_ERR_INVALID_ARGS);
    return;
  }

  zxlogf(TRACE, "Sending %zu bytes to endpoint 0x%08" PRIx8 "", length, tx_endpoint_->addr);

  {
    fbl::AutoLock tx_lock(&tx_mutex_);
    if (unbound_) {
      status = ZX_ERR_IO_NOT_PRESENT;
    } else {
      status = SendLocked(netbuf);
      if (status == ZX_ERR_SHOULD_WAIT) {
        // No buffers available, queue it up
        list_add_tail(&tx_pending_infos_, &txn->node);
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
      status = SetPacketFilterMode(USB_CDC_PACKET_TYPE_PROMISCUOUS, static_cast<bool>(value));
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
  }

  return status;
}

zx_status_t UsbCdcEcm::QueueRequest(const uint8_t* data, size_t length, usb_request_t* req) {
  req->header.length = length;
  if (ethernet_ifc_.ops == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  ssize_t bytes_copied = usb_request_copy_to(req, data, length, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "Failed to copy data into send txn (error %zd)", bytes_copied);
    return ZX_ERR_IO;
  }
  usb_request_complete_callback_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<UsbCdcEcm*>(ctx)->UsbWriteComplete(request);
          },
      .ctx = this,
  };
  usb_.RequestQueue(req, &complete);
  return ZX_OK;
}

void UsbCdcEcm::UsbReceive(usb_request_t* request) {
  size_t len = request->response.actual;

  void* read_data;
  zx_status_t status = usb_request_mmap(request, &read_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_mmap failed with status: %s", zx_status_get_string(status));
    return;
  }

  fbl::AutoLock ethernet_lock(&ethernet_mutex_);
  if (ethernet_ifc_.ops) {
    ethernet_ifc_recv(&ethernet_ifc_, static_cast<uint8_t*>(read_data), len, 0);
  }
}

zx_status_t UsbCdcEcm::SetPacketFilterMode(uint16_t mode, bool on) {
  zx_status_t status = ZX_OK;
  uint16_t bits = rx_packet_filter_;

  if (on) {
    bits |= mode;
  } else {
    bits &= ~mode;
  }

  status =
      usb_.ControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                      USB_CDC_SET_ETHERNET_PACKET_FILTER, bits, 0, ZX_TIME_INFINITE, nullptr, 0);

  if (status != ZX_OK) {
    zxlogf(ERROR, "Set packet filter failed: %d", status);
    return status;
  }
  rx_packet_filter_ = bits;
  return status;
}

void UsbCdcEcm::HandleInterrupt(usb_request_t* request) {
  if (request->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(DEBUG, "Ignored interrupt (size = %ld)", (long)request->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req = {};
  __UNUSED size_t result =
      usb_request_copy_from(request, &usb_req, sizeof(usb_cdc_notification_t), 0);
  if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
      usb_req.bNotification == USB_CDC_NC_NETWORK_CONNECTION) {
    UpdateOnlineStatus(usb_req.wValue != 0);
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
    if (new_us_bps != us_bps_) {
      zxlogf(INFO, "Connection speed change... upstream bits/s: %" PRIu32 "", new_us_bps);
      us_bps_ = new_us_bps;
    }
    if (new_ds_bps != ds_bps_) {
      zxlogf(INFO, "Connection speed change... downstream bits/s: %" PRIu32 "", new_ds_bps);
      ds_bps_ = new_ds_bps;
    }
  } else {
    zxlogf(ERROR, "Ignored interrupt (type = %" PRIu8 ", request = %" PRIu8 ")",
           usb_req.bmRequestType, usb_req.bNotification);
    return;
  }
}

int UsbCdcEcm::InterruptThread() {
  usb_request_t* req = int_txn_buf_;

  usb_request_complete_callback_t complete = {
      .callback = [](void* ctx, usb_request_t* request) -> void {
        static_cast<UsbCdcEcm*>(ctx)->InterruptComplete(request);
      },
      .ctx = this,
  };
  while (true) {
    sync_completion_reset(&completion_);
    usb_.RequestQueue(req, &complete);
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);
    if (req->response.status == ZX_OK) {
      HandleInterrupt(req);
    } else if (req->response.status == ZX_ERR_PEER_CLOSED ||
               req->response.status == ZX_ERR_IO_NOT_PRESENT) {
      zxlogf(DEBUG, "Terminating interrupt handling thread");
      return req->response.status;
    } else if (req->response.status == ZX_ERR_IO_REFUSED ||
               req->response.status == ZX_ERR_IO_INVALID) {
      zxlogf(DEBUG, "Resetting interrupt endpoint");
      usb_.ResetEndpoint(int_endpoint_->addr);
    } else {
      zxlogf(ERROR, "Error waiting for interrupt - ignoring: %s",
             zx_status_get_string(req->response.status));
    }
  }
}

void UsbCdcEcm::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = Init();
  txn.Reply(status);
}

zx_status_t UsbCdcEcm::Init() {
  zxlogf(DEBUG, "Starting %s", __FUNCTION__);

  // Initialize context
  list_initialize(&tx_txn_bufs_);
  list_initialize(&tx_pending_infos_);
  zx_status_t result = usb_.ControlOut(
      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_CDC_SET_ETHERNET_PACKET_FILTER,
      kEthernetInitialPacketFilter, 0, ZX_TIME_INFINITE, nullptr, 0);
  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to set initial packet filter: %s", zx_status_get_string(result));
    return result;
  }
  rx_packet_filter_ = kEthernetInitialPacketFilter;
  parent_req_size_ = usb_.GetRequestSize();

  // Find the CDC descriptors and endpoints
  auto parser = UsbCdcDescriptorParser::Parse(usb_);
  if (parser.is_error()) {
    zxlogf(ERROR, "Failed to parse usb descriptor: %s", parser.status_string());
    return parser.error_value();
  }

  // Parse endpoint information
  int_endpoint_ = parser->GetInterruptEndpoint();
  tx_endpoint_ = parser->GetTxEndpoint();
  rx_endpoint_ = parser->GetRxEndpoint();

  EcmInterface default_ifc = parser->GetDefaultInterface();
  EcmInterface data_ifc = parser->GetDataInterface();

  mtu_ = parser->GetMtu();
  mac_addr_ = parser->GetMacAddress();

  rx_endpoint_delay_ = kEthernetInitialRecvDelay;
  tx_endpoint_delay_ = kEthernetInitialTransmitDelay;

  // Reset by selecting default interface followed by data interface. We can't start
  // queueing transactions until this is complete.
  usb_.SetInterface(default_ifc.number, default_ifc.alternate_setting);
  usb_.SetInterface(data_ifc.number, data_ifc.alternate_setting);

  // Allocate interrupt transaction buffer
  usb_request_t* int_buf;
  uint64_t req_size = parent_req_size_ + sizeof(usb_req_internal_t);
  result =
      usb_request_alloc(&int_buf, int_endpoint_->max_packet_size, int_endpoint_->addr, req_size);
  if (result != ZX_OK) {
    return result;
  }

  int_txn_buf_ = int_buf;

  // Allocate tx transaction buffers
  uint16_t tx_buf_sz = mtu_;
  if (tx_buf_sz > kMaxTxBufferSize) {
    zxlogf(ERROR, "Insufficient space for even a single tx buffer");
    return result;
  }
  size_t tx_buf_remain = kMaxTxBufferSize;
  while (tx_buf_remain >= tx_buf_sz) {
    usb_request_t* tx_buf;
    result = usb_request_alloc(&tx_buf, tx_buf_sz, tx_endpoint_->addr, req_size);
    tx_buf->direct = true;
    if (result != ZX_OK) {
      return result;
    }

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
    // transmission when the endpoint max packet size is a factor of the total transmission size
    tx_buf->header.send_zlp = true;
    {
      fbl::AutoLock _(&tx_mutex_);
      zx_status_t add_result = usb_req_list_add_head(&tx_txn_bufs_, tx_buf, parent_req_size_);
      ZX_DEBUG_ASSERT(add_result == ZX_OK);
    }

    tx_buf_remain -= tx_buf_sz;
  }

  // Allocate rx transaction buffers
  uint32_t rx_buf_sz = mtu_;
  if (rx_buf_sz > kMaxRxBufferSize) {
    zxlogf(ERROR, "Insufficient space for even a single rx buffer");
    return ZX_ERR_NO_MEMORY;
  }

  usb_request_complete_callback_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<UsbCdcEcm*>(ctx)->UsbReadComplete(request);
          },
      .ctx = this,
  };
  size_t rx_buf_remain = kMaxRxBufferSize;
  while (rx_buf_remain >= rx_buf_sz) {
    usb_request_t* rx_buf;
    result = usb_request_alloc(&rx_buf, rx_buf_sz, rx_endpoint_->addr, req_size);
    if (result != ZX_OK) {
      return result;
    }
    rx_buf->direct = true;
    usb_.RequestQueue(rx_buf, &complete);
    rx_buf_remain -= rx_buf_sz;
  }
  // Kick off the handler thread
  int thread_result = thrd_create_with_name(
      &int_thread_,
      [](void* arg) -> int { return static_cast<UsbCdcEcm*>(arg)->InterruptThread(); }, this,
      "ecm_int_handler_thread");
  if (thread_result != thrd_success) {
    zxlogf(ERROR, "Failed to create interrupt handler thread (%d)", thread_result);
    return ZX_ERR_NO_RESOURCES;
  }

  return ZX_OK;
}

zx_status_t UsbCdcEcm::Bind(void* ctx, zx_device_t* parent) {
  zxlogf(DEBUG, "Starting %s", __FUNCTION__);

  usb::UsbDevice usb;
  zx_status_t status = usb::UsbDevice::CreateFromDevice(parent, &usb);
  if (status != ZX_OK) {
    return status;
  }

  auto device = std::make_unique<UsbCdcEcm>(parent, usb);

  usb_composite_protocol_t usb_composite;
  status = device_get_protocol(parent, ZX_PROTOCOL_USB_COMPOSITE, &usb_composite);
  if (status != ZX_OK) {
    return status;
  }

  status = usb_claim_additional_interfaces(&usb_composite, WantInterface, NULL);
  if (status != ZX_OK) {
    return status;
  }

  auto args = ddk::DeviceAddArgs("usb-cdc-ecm").set_proto_id(ZX_PROTOCOL_ETHERNET_IMPL);

  // TODO(fxbug.dev/93333): Remove this conditional once GND is ready everywhere.
  const zx_device_str_prop_t props[] = {
      {
          .key = "fuchsia.ethernet.NETDEVICE_MIGRATION",
          .property_value = str_prop_bool_val(true),
      },
  };
  if (device_is_dfv2(parent)) {
    args.set_str_props(props);
  }

  status = device->DdkAdd(args);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Error adding device: %s", __func__, zx_status_get_string(status));
    return status;
  }

  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  __UNUSED auto unused = device.release();

  return ZX_OK;
}

zx_status_t UsbCdcEcm::SendLocked(ethernet_netbuf_t* netbuf) {
  // Make sure that we can get all of the tx buffers we need to use
  usb_request_t* req = usb_req_list_remove_head(&tx_txn_bufs_, parent_req_size_);
  if (req == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));
  zx_status_t status = QueueRequest(netbuf->data_buffer, netbuf->data_size, req);
  if (status != ZX_OK) {
    zx_status_t add_status = usb_req_list_add_tail(&tx_txn_bufs_, req, parent_req_size_);
    ZX_DEBUG_ASSERT(add_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

void UsbCdcEcm::UsbReadComplete(usb_request_t* request) __TA_NO_THREAD_SAFETY_ANALYSIS {
  if (request->response.status != ZX_OK) {
    zxlogf(DEBUG, "UsbReadComplete called with status %s",
           zx_status_get_string(request->response.status));
  }

  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "Resetting receive endpoint");
    request->reset = true;
    request->reset_address = rx_endpoint_->addr;
    usb_request_complete_callback_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              static_cast<UsbCdcEcm*>(ctx)->UsbReadComplete(request);
            },
        .ctx = this,
    };
    usb_.RequestQueue(request, &complete);
    return;
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    if (rx_endpoint_delay_ < kEthernetMaxRecvDelay) {
      rx_endpoint_delay_ += kEthernetMaxRecvDelay;
    }
    zxlogf(DEBUG, "Slowing down the requests by %lu usec. Resetting the recv endpoint",
           kEthernetRecvDelay);
    request->reset = true;
    request->reset_address = rx_endpoint_->addr;
    usb_request_complete_callback_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              static_cast<UsbCdcEcm*>(ctx)->UsbReadComplete(request);
            },
        .ctx = this,
    };
    usb_.RequestQueue(request, &complete);
    return;
  } else if (request->response.status == ZX_OK && !request->reset) {
    UsbReceive(request);
  }
  if (rx_endpoint_delay_) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(rx_endpoint_delay_)));
  }
  request->reset = false;
  usb_request_complete_callback_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<UsbCdcEcm*>(ctx)->UsbReadComplete(request);
          },
      .ctx = this,
  };
  usb_.RequestQueue(request, &complete);
}

void UsbCdcEcm::UsbWriteComplete(usb_request_t* request) {
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  bool additional_tx_queued = false;
  txn_info_t* txn;
  zx_status_t send_status = ZX_OK;
  {
    fbl::AutoLock tx_lock(&tx_mutex_);
    // If reset, we still hold the TX mutex.
    if (!request->reset) {
      // Return transmission buffer to pool
      zx_status_t status = usb_req_list_add_tail(&tx_txn_bufs_, request, parent_req_size_);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(DEBUG, "Resetting transmit endpoint");
        request->reset = true;
        request->reset_address = tx_endpoint_->addr;
        usb_request_complete_callback_t complete = {
            .callback = [](void* arg, usb_request_t* request) -> void {
              static_cast<UsbCdcEcm*>(arg)->UsbWriteComplete(request);
            },
            .ctx = this,
        };
        usb_.RequestQueue(request, &complete);
        return;
      }

      if (request->response.status == ZX_ERR_IO_INVALID) {
        zxlogf(DEBUG, "Slowing down the requests by %lu usec. Resetting the transmit endpoint",
               kEthernetTransmitDelay);
        if (tx_endpoint_delay_ < kEthernetMaxTransmitDelay) {
          tx_endpoint_delay_ += kEthernetTransmitDelay;
        }
        request->reset = true;
        request->reset_address = tx_endpoint_->addr;
        usb_request_complete_callback_t complete = {
            .callback = [](void* arg, usb_request_t* request) -> void {
              static_cast<UsbCdcEcm*>(arg)->UsbWriteComplete(request);
            },
            .ctx = this,
        };
        usb_.RequestQueue(request, &complete);
        return;
      }
    }
    request->reset = false;

    if (!list_is_empty(&tx_pending_infos_)) {
      txn = list_peek_head_type(&tx_pending_infos_, txn_info_t, node);
      if ((send_status = SendLocked(&txn->netbuf)) != ZX_ERR_SHOULD_WAIT) {
        list_remove_head(&tx_pending_infos_);
        additional_tx_queued = true;
      }
    }
  }

  fbl::AutoLock ethernet_lock(&ethernet_mutex_);
  if (additional_tx_queued) {
    CompleteTxn(txn, send_status);
  }

  // When the interface is offline, the transaction will complete with status set to
  // ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

}  // namespace usb_cdc_ecm

static zx_driver_ops_t ecm_driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = usb_cdc_ecm::UsbCdcEcm::Bind;
  return ops;
}();

ZIRCON_DRIVER(ethernet_usb_cdc_ecm, ecm_driver_ops, "zircon", "0.1");
