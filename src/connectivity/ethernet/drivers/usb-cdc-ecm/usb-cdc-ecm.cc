// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/usb-cdc-ecm/usb-cdc-ecm.h"

#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/composite/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <lib/operation/ethernet.h>
#include <sys/types.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <usb/cdc.h>
#include <usb/request-cpp.h>
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

static bool WantInterface(usb_interface_descriptor_t* intf, void* arg) {
  return intf->b_interface_class == USB_CLASS_CDC;
}

void UsbCdcEcm::DdkUnbind(ddk::UnbindTxn unbind_txn) {
  // TODO: Instead of taking the lock here and holding up the whole driver host, offload to the
  // worker thread and take ownership of the UnbindTxn.
  fbl::AutoLock tx_lock(&mutex_);
  unbound_ = true;
  unbind_txn.Reply();
}

UsbCdcEcm::~UsbCdcEcm() {
  if (int_thread_) {
    thrd_join(int_thread_, nullptr);
  }
}

void UsbCdcEcm::DdkRelease() { delete this; }

void UsbCdcEcm::UpdateOnlineStatus(bool is_online) {
  fbl::AutoLock lock(&mutex_);
  fbl::AutoLock ethernet_lock(&ethernet_mutex_);
  if ((is_online && online_) || (!is_online && !online_)) {
    return;
  }

  usb_request_complete_callback_t callback = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<UsbCdcEcm*>(ctx)->UsbReadComplete(request);
          },
      .ctx = this,
  };

  if (is_online) {
    zxlogf(INFO, "Connected to network");
    online_ = true;

    std::optional<usb::Request<>> request;
    size_t request_size = usb::Request<>::RequestSize(parent_req_size_);
    while ((request = rx_request_pool_.Get(request_size))) {
      usb_.RequestQueue(request->take(), &callback);
    }

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
  info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));

  return ZX_OK;
}

void UsbCdcEcm::EthernetImplStop() {
  fbl::AutoLock tx_lock(&mutex_);
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
  zxlogf(TRACE, "%s called", __FUNCTION__);
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));

  size_t length = op.operation()->data_size;
  if (length > mtu_ || length == 0) {
    op.Complete(ZX_ERR_INVALID_ARGS);
    return;
  }

  zxlogf(TRACE, "Sending %zu bytes to endpoint 0x%08" PRIx8 "", length, tx_endpoint_->addr);

  fbl::AutoLock lock(&mutex_);

  if (!pending_tx_queue_.is_empty()) {
    pending_tx_queue_.push(std::move(op));
    return;
  }

  if (unbound_) {
    lock.release();
    op.Complete(ZX_ERR_IO_NOT_PRESENT);
  } else {
    zx_status_t status = SendLocked(op);
    if (status == ZX_ERR_SHOULD_WAIT) {
      pending_tx_queue_.push(std::move(op));
    } else {
      op.Complete(ZX_OK);
    }
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

void UsbCdcEcm::HandleInterrupt(usb::Request<void>& request) {
  if (request.request()->response.actual < sizeof(usb_cdc_notification_t)) {
    zxlogf(DEBUG, "Ignored interrupt (size = %lu)", request.request()->response.actual);
    return;
  }

  usb_cdc_notification_t usb_req = {};
  ssize_t result = request.CopyFrom(&usb_req, sizeof(usb_cdc_notification_t), 0);
  if (result != static_cast<ssize_t>(sizeof(usb_cdc_notification_t))) {
    zxlogf(DEBUG, "Ignored interrupt (copied %ld from request)", result);
    return;
  }

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
    result = request.CopyFrom(&new_us_bps, sizeof(new_us_bps), sizeof(usb_cdc_notification_t));
    if (result != static_cast<ssize_t>(sizeof(uint32_t))) {
      zxlogf(ERROR, "Failed to read new upstream speed: %ld", result);
      return;
    }

    result = request.CopyFrom(&new_us_bps, sizeof(new_us_bps),
                              sizeof(usb_cdc_notification_t) + sizeof(uint32_t));
    if (result != static_cast<ssize_t>(sizeof(uint32_t))) {
      zxlogf(ERROR, "Failed to read new downstream speed: %ld", result);
      return;
    }

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
  usb_request_complete_callback_t complete = {
      .callback = [](void* ctx, usb_request_t* request) -> void {
        static_cast<UsbCdcEcm*>(ctx)->InterruptComplete(request);
      },
      .ctx = this,
  };

  while (true) {
    // Pass ownership of request to parent device.
    sync_completion_reset(&completion_);
    usb_.RequestQueue(interrupt_request_->request(), &complete);

    // Parent device has handed ownership of request back to us.
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);

    zx_status_t request_status = interrupt_request_->request()->response.status;
    if (request_status == ZX_OK) {
      HandleInterrupt(interrupt_request_.value());
    } else if (request_status == ZX_ERR_PEER_CLOSED || request_status == ZX_ERR_IO_NOT_PRESENT) {
      zxlogf(DEBUG, "Terminating interrupt handling thread");
      return request_status;
    } else if (request_status == ZX_ERR_IO_REFUSED || request_status == ZX_ERR_IO_INVALID) {
      zxlogf(DEBUG, "Resetting interrupt endpoint");
      usb_.ResetEndpoint(int_endpoint_->addr);
    } else {
      zxlogf(ERROR, "Error waiting for interrupt - ignoring: %s",
             zx_status_get_string(request_status));
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
  zx_status_t status = usb_.ControlOut(
      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_CDC_SET_ETHERNET_PACKET_FILTER,
      kEthernetInitialPacketFilter, 0, ZX_TIME_INFINITE, nullptr, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to set initial packet filter: %s", zx_status_get_string(status));
    return status;
  }
  rx_packet_filter_ = kEthernetInitialPacketFilter;

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
  parent_req_size_ = usb_.GetRequestSize();
  status = usb::Request<void>::Alloc(&interrupt_request_, int_endpoint_->max_packet_size,
                                     int_endpoint_->addr, parent_req_size_);
  if (status != ZX_OK) {
    return status;
  }

  // Allocate tx transaction buffers
  uint16_t tx_buf_sz = mtu_;
  if (tx_buf_sz > kMaxTxBufferSize) {
    zxlogf(ERROR, "Insufficient space for even a single tx buffer");
    return status;
  }

  fbl::AutoLock lock(&mutex_);

  size_t tx_buf_remain = kMaxTxBufferSize;
  while (tx_buf_remain >= tx_buf_sz) {
    std::optional<usb::Request<void>> request;
    status = usb::Request<void>::Alloc(&request, tx_buf_sz, tx_endpoint_->addr, parent_req_size_);
    if (status != ZX_OK) {
      return status;
    }
    request->request()->direct = true;

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
    // transmission when the endpoint max packet size is a factor of the total transmission size
    request->request()->header.send_zlp = true;
    tx_request_pool_.Add(*std::move(request));

    tx_buf_remain -= tx_buf_sz;
  }

  // Allocate rx transaction buffers
  uint32_t rx_buf_sz = mtu_;
  if (rx_buf_sz > kMaxRxBufferSize) {
    zxlogf(ERROR, "Insufficient space for even a single rx buffer");
    return ZX_ERR_NO_MEMORY;
  }

  size_t rx_buf_remain = kMaxRxBufferSize;
  while (rx_buf_remain >= rx_buf_sz) {
    std::optional<usb::Request<void>> request;
    status = usb::Request<void>::Alloc(&request, rx_buf_sz, rx_endpoint_->addr, parent_req_size_);
    if (status != ZX_OK) {
      return status;
    }

    request->request()->direct = true;
    rx_request_pool_.Add(*std::move(request));
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

zx_status_t UsbCdcEcm::SendLocked(const eth::BorrowedOperation<void>& op) {
  // Make sure that we can get all of the tx buffers we need to use
  std::optional<usb::Request<>> request;
  request = tx_request_pool_.Get(usb::Request<>::RequestSize(parent_req_size_));
  if (!request.has_value()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));

  {
    fbl::AutoLock ethernet_lock(&ethernet_mutex_);
    if (ethernet_ifc_.ops == nullptr) {
      zxlogf(ERROR, "No ethernet interface during QueueTx");
      tx_request_pool_.Add(*std::move(request));
      return ZX_ERR_BAD_STATE;
    }
  }

  request->request()->header.length = op.operation()->data_size;
  ssize_t bytes_copied = request->CopyTo(op.operation()->data_buffer, op.operation()->data_size, 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "Failed to copy data into send txn (error %zd)", bytes_copied);
    tx_request_pool_.Add(*std::move(request));
    return ZX_ERR_IO;
  }

  usb_request_complete_callback_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<UsbCdcEcm*>(ctx)->UsbWriteComplete(request);
          },
      .ctx = this,
  };
  usb_.RequestQueue(request->take(), &complete);
  return ZX_OK;
}

void UsbCdcEcm::UsbReadComplete(usb_request_t* usb_request) {
  usb::Request<> request(usb_request, parent_req_size_);

  if (request.request()->response.status != ZX_OK) {
    zxlogf(DEBUG, "UsbReadComplete called with status %s",
           zx_status_get_string(request.request()->response.status));
  }

  if (request.request()->response.status == ZX_ERR_IO_NOT_PRESENT) {
    zxlogf(WARNING, "USB device not present");
    // The device has gone away, instead of requeueing the request - add it back to the pool. If the
    // device comes back online it will be queued then.
    fbl::AutoLock lock(&mutex_);
    rx_request_pool_.Add(std::move(request));
    return;
  }

  auto request_cleanup = fit::defer([this, &request]() {
    usb_request_complete_callback_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              static_cast<UsbCdcEcm*>(ctx)->UsbReadComplete(request);
            },
        .ctx = this,
    };
    usb_.RequestQueue(request.take(), &complete);
  });

  if (request.request()->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "Resetting receive endpoint");
    usb_.ResetEndpoint(rx_endpoint_->addr);
    return;
  } else if (request.request()->response.status == ZX_ERR_IO_INVALID) {
    if (rx_endpoint_delay_ < kEthernetMaxRecvDelay) {
      rx_endpoint_delay_ += kEthernetMaxRecvDelay;
    }
    zxlogf(DEBUG, "Slowing down the requests by %lu usec. Resetting the recv endpoint",
           kEthernetRecvDelay);
    usb_.ResetEndpoint(rx_endpoint_->addr);
    return;
  } else if (request.request()->response.status != ZX_OK) {
    zxlogf(WARNING, "USB request status: %s",
           zx_status_get_string(request.request()->response.status));
    return;
  }

  void* read_data;
  const size_t len = request.request()->response.actual;
  const zx_status_t status = request.Mmap(&read_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "request.Mmap failed with status: %s", zx_status_get_string(status));
    return;
  }

  {
    fbl::AutoLock ethernet_lock(&ethernet_mutex_);
    if (ethernet_ifc_.ops) {
      ethernet_ifc_recv(&ethernet_ifc_, static_cast<uint8_t*>(read_data), len, 0);
    }
  }

  // Delay before requeueing the request.
  if (rx_endpoint_delay_) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(rx_endpoint_delay_)));
  }
}

void UsbCdcEcm::UsbWriteComplete(usb_request_t* usb_request) {
  usb::Request<> request(usb_request, parent_req_size_);

  if (request.request()->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "Resetting transmit endpoint");
    usb_.ResetEndpoint(tx_endpoint_->addr);

  } else if (request.request()->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG, "Slowing down the requests by %lu usec. Resetting the transmit endpoint",
           kEthernetTransmitDelay);
    if (tx_endpoint_delay_ < kEthernetMaxTransmitDelay) {
      tx_endpoint_delay_ += kEthernetTransmitDelay;
    }
    usb_.ResetEndpoint(tx_endpoint_->addr);
  }

  // Return transmission buffer to pool
  fbl::AutoLock tx_lock(&mutex_);
  tx_request_pool_.Add(std::move(request));

  while (!pending_tx_queue_.is_empty()) {
    auto op = pending_tx_queue_.pop().value();
    zx_status_t status = SendLocked(op);
    if (status == ZX_ERR_SHOULD_WAIT) {
      pending_tx_queue_.push_next(std::move(op));
      break;
    }
    op.Complete(status);
  }
}

}  // namespace usb_cdc_ecm

static zx_driver_ops_t ecm_driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = usb_cdc_ecm::UsbCdcEcm::Bind;
  return ops;
}();

ZIRCON_DRIVER(ethernet_usb_cdc_ecm, ecm_driver_ops, "zircon", "0.1");
