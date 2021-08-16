// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hub.h"

#include <fuchsia/hardware/usb/hubdescriptor/c/banjo.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>
#include <string>

#include <fbl/auto_lock.h>
#include <fbl/hard_int.h>
#include <usb/usb.h>

#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include "src/devices/usb/drivers/usb-hub-rewrite/usb_hub_rewrite_bind.h"

namespace usb_hub {

usb_speed_t PortStatus::GetSpeed(usb_speed_t hub_speed) const {
  usb_speed_t speed;
  if (hub_speed == USB_SPEED_SUPER) {
    speed = USB_SPEED_SUPER;
  } else if (status & USB_PORT_LOW_SPEED) {
    speed = USB_SPEED_LOW;
  } else if (status & USB_PORT_HIGH_SPEED) {
    speed = USB_SPEED_HIGH;
  } else {
    speed = USB_SPEED_FULL;
  }
  return speed;
}

void PortStatus::Reset() {
  connected = false;
  reset_pending = false;
  enumeration_pending = false;
  link_active = true;
}

zx_status_t UsbHubDevice::Init() {
  usb_ = ddk::UsbProtocolClient(parent());
  bus_ = ddk::UsbBusProtocolClient(parent());
  zx_status_t status =
      DdkAdd(ddk::DeviceAddArgs("usb-hub").set_inspect_vmo(inspector_.DuplicateVmo()));
  return status;
}

zx_status_t UsbHubDevice::UsbHubInterfaceResetPort(uint32_t port) {
  return SetFeature(USB_RECIP_PORT, USB_FEATURE_PORT_RESET, static_cast<uint8_t>(port));
}

zx_status_t UsbHubDevice::GetPortStatus() {
  std::vector<zx::status<usb_port_status_t>> pending_actions;
  pending_actions.reserve(hub_descriptor_.b_nbr_ports);
  for (uint8_t i = 1; i <= hub_descriptor_.b_nbr_ports; i++) {
    pending_actions.push_back(GetPortStatus(PortNumber(i)));
  }
  size_t index = 0;
  for (auto& result : pending_actions) {
    if (result.is_error()) {
      return result.error_value();
    }
    fbl::AutoLock l(&async_execution_context_);
    ZX_ASSERT(index <= port_status_.size());
    port_status_[index].status = result.value().w_port_status;
    index++;
  }
  return ZX_OK;
}

void UsbHubDevice::DdkInit(ddk::InitTxn txn) {
  txn_ = std::move(txn);
  // First -- configure all endpoints and initialize the hub interface
  zx_status_t status = loop_.StartThread();
  if (status != ZX_OK) {
    txn_->Reply(status);
    return;
  }
  executor_ = std::make_unique<async::Executor>(loop_.dispatcher());

  std::optional<usb::InterfaceList> interfaces;
  status = usb::InterfaceList::Create(usb_, false, &interfaces);
  if (status != ZX_OK) {
    txn_->Reply(status);
    return;
  }
  status = ZX_ERR_IO;
  // According to USB 2.0 Specification section 11.12.1 a hub should have exactly one
  // interrupt endpoint and no other endpoints.
  for (auto& interface : *interfaces) {
    if (interface.descriptor()->b_num_endpoints == 1) {
      auto eplist = interface.GetEndpointList();
      auto ep_iter = eplist.begin();
      auto ep = ep_iter.endpoint();
      interrupt_endpoint_ = ep->descriptor;
      status = usb_.EnableEndpoint(&interrupt_endpoint_,
                                   (ep->has_companion) ? &ep->ss_companion : nullptr, true);
      break;
    }
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "Initialization failed due to %s", zx_status_get_string(status));
    txn_->Reply(status);
  }
  speed_ = usb_.GetSpeed();
  uint16_t desc_type = (speed_ == USB_SPEED_SUPER ? USB_HUB_DESC_TYPE_SS : USB_HUB_DESC_TYPE);

  auto result = GetUsbHubDescriptor(desc_type);
  if (result.is_error()) {
    zxlogf(ERROR, "Could not create usb hub descriptor");
    txn_->Reply(result.error_value());
    return;
  }
  usb_hub_descriptor_t descriptor = result.value();
  fbl::AutoLock l(&async_execution_context_);
  hub_descriptor_ = descriptor;
  {
    port_status_ = fbl::Array<PortStatus>(new PortStatus[hub_descriptor_.b_nbr_ports],
                                          hub_descriptor_.b_nbr_ports);
  }
  auto raw_desc = descriptor;

  // Config bus
  status = bus_.SetHubInterface(reinterpret_cast<uint64_t>(zxdev()), this,
                                &usb_hub_interface_protocol_ops_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not set hub interface");
    txn_->Reply(status);
    return;
  }
  // TODO (fxbug.dev/56002): Support multi-TT hubs properly. Currently, we
  // operate in single-TT mode even if the hub supports multiple TTs.
  status = bus_.ConfigureHub(reinterpret_cast<uint64_t>(zxdev()), speed_, &raw_desc, false);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not configure hub");
    txn_->Reply(status);
    return;
  }

  // Once the hub is initialized, power on the ports and wait as per spec

  status = PowerOnPorts();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not power on all ports");
    txn_->Reply(status);
    return;
  }

  status = zx::nanosleep(zx::deadline_after(zx::msec(2 * hub_descriptor_.b_power_on2_pwr_good)));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to sleep");
    txn_->Reply(status);
    return;
  }

  // Next -- we retrieve the port status
  status = GetPortStatus();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get port status\n");
    txn_->Reply(status);
    return;
  }

  // Finally -- we can start the interrupt loop
  // and bringup our initial set of devices
  status = StartInterruptLoop();
  if (status != ZX_OK) {
    txn_->Reply(status);
    return;
  }
  for (size_t i = 0; i < port_status_.size(); i++) {
    HandlePortStatusChanged(IndexToPortNumber(PortArrayIndex(static_cast<uint8_t>(i))));
  }
  txn_->Reply(ZX_OK);
}

void UsbHubDevice::HandlePortStatusChanged(PortNumber port) {
  const auto& status = port_status_[PortNumberToIndex(port).value()];
  if (!status.connected && (status.status & USB_C_PORT_CONNECTION)) {
    HandleDeviceConnected(port);
  }
  if (status.connected && !(status.status & USB_C_PORT_CONNECTION)) {
    HandleDeviceDisconnected(port);
  }
  if (status.reset_pending && (status.status & USB_C_PORT_ENABLE) &&
      !(status.status & USB_C_PORT_RESET)) {
    HandleResetComplete(port);
  }
}

void UsbHubDevice::InterruptCallback() {
  // Data to be extracted from the request
  size_t hub_bit_count =
      hub_descriptor_.b_nbr_ports + 1;  // Number of ports including the hub itself
  size_t byte_padding =
      ((hub_bit_count - 1) / sizeof(uint8_t)) + 1;  // byte padding for handling response
  auto data = std::make_unique<uint8_t[]>(hub_bit_count + byte_padding);
  zx_status_t data_status = ZX_OK;
  size_t data_length;
  auto handleCallback = [&](CallbackRequest cr) mutable {
    if (shutting_down_ || cr.request()->response.status != ZX_OK) {
      sync_completion_signal(&xfer_done_);
      return;
    }
    data_status = cr.request()->response.status;
    data_length = cr.request()->response.actual;
    if (data_length > hub_bit_count) {
      data_status = ZX_ERR_BAD_STATE;
      zxlogf(ERROR, "Data received from request is more than number of ports");
      return;
    }
    size_t val = cr.CopyFrom(data.get(), cr.request()->response.actual, 0);
    if (val != data_length) {
      data_status = ZX_ERR_BAD_STATE;
      zxlogf(ERROR, "Could not copy data correctly");
      return;
    }
    sync_completion_signal(&xfer_done_);
    cr.Queue(usb_);
  };
  // Run until device unbinds
  std::optional<CallbackRequest> request;
  CallbackRequest::Alloc(&request, usb_ep_max_packet(&interrupt_endpoint_),
                         interrupt_endpoint_.b_endpoint_address, usb_.GetRequestSize(),
                         handleCallback);
  request->Queue(usb_);
  while (!shutting_down_) {
    sync_completion_wait(&xfer_done_, ZX_TIME_INFINITE);
    sync_completion_reset(&xfer_done_);

    if (shutting_down_ || data_status != ZX_OK) {
      return;
    }

    // bit zero is hub status
    if (data[0] & hubStatusBit) {
      // TODO(fxbug.dev/58148) what to do here?
      zxlogf(ERROR, "usb_hub_interrupt_complete hub status changed");
    }
    // Iterate through the bitmap (bitmap length and port count is the same)
    for (uint8_t port = 1; port <= hub_descriptor_.b_nbr_ports; port++) {
      uint8_t bit = port % 8;
      uint8_t byte = port / 8;
      if (data[byte] & (1 << bit)) {
        auto port_result = GetPortStatus(PortNumber(static_cast<uint8_t>(port)));
        if (port_result.is_error()) {
          zxlogf(ERROR, "Could not get port status");
          return;
        }
        auto port_status = port_result.value();
        auto port_number = PortNumber(static_cast<uint8_t>(port));
        fbl::AutoLock l(&async_execution_context_);
        port_status_[PortNumberToIndex(port_number).value()].status = port_status.w_port_status;
        HandlePortStatusChanged(port_number);
      }
    }
  }
}

zx_status_t UsbHubDevice::StartInterruptLoop() {
  auto callback_func = [](void* ctx) {
    static_cast<UsbHubDevice*>(ctx)->InterruptCallback();
    return 0;
  };
  int thread_status =
      thrd_create_with_name(&callback_thread_, callback_func, this, "usb-hub-interrupt");
  if (thread_status != thrd_success) {
    zxlogf(ERROR, "Could not create thread to handle port changes");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t UsbHubDevice::ResetPort(PortNumber port) {
  zx_status_t status = SetFeature(USB_RECIP_PORT, USB_FEATURE_PORT_RESET, port.value());
  if (status != ZX_OK) {
    return status;
  }
  fbl::AutoLock l(&async_execution_context_);
  port_status_[PortNumberToIndex(port).value()].reset_pending = true;
  return ZX_OK;
}

PortNumber UsbHubDevice::GetPortNumber(const PortStatus& status) {
  // This is safe even from non-async context as we are not actually accessing
  // the data in port_status, just fetching the pointer.
  fbl::AutoLock l(&async_execution_context_);
  return PortNumber(static_cast<uint8_t>(&status - port_status_.data()) + 1);
}

void UsbHubDevice::EnumerateNext() {
  if (!pending_enumeration_list_.is_empty()) {
    BeginEnumeration(GetPortNumber(pending_enumeration_list_.front()));
  }
}

void UsbHubDevice::BeginEnumeration(PortNumber port) {
  zx_status_t status = ResetPort(port);
  if (status != ZX_OK) {
    // Port reset failed -- stop enumeration and enumerate the next device.
    fbl::AutoLock l(&async_execution_context_);
    pending_enumeration_list_.erase(port_status_[PortNumberToIndex(port).value()]);
    EnumerateNext();
  }
}

void UsbHubDevice::HandleDeviceConnected(PortNumber port) {
  auto& status = port_status_[PortNumberToIndex(port).value()];
  status.connected = true;
  bool was_empty = pending_enumeration_list_.is_empty();
  pending_enumeration_list_.push_back(&status);
  if (was_empty) {
    EnumerateNext();
  }
}

void UsbHubDevice::HandleDeviceDisconnected(PortNumber port) {
  bool link_status = port_status_[PortNumberToIndex(port).value()].link_active;
  port_status_[PortNumberToIndex(port).value()].Reset();
  if (link_status) {
    bus_.DeviceRemoved(reinterpret_cast<uint64_t>(zxdev()), port.value());
  }
}

void UsbHubDevice::HandleResetComplete(PortNumber port) {
  port_status_[PortNumberToIndex(port).value()].reset_pending = false;
  port_status_[PortNumberToIndex(port).value()].enumeration_pending = true;
  auto speed = port_status_[PortNumberToIndex(port).value()].GetSpeed(speed_);
  zx_status_t status = bus_.DeviceAdded(reinterpret_cast<uint64_t>(zxdev()), port.value(), speed);

  port_status_[PortNumberToIndex(port).value()].enumeration_pending = false;
  port_status_[PortNumberToIndex(port).value()].link_active = (status == ZX_OK);
  pending_enumeration_list_.erase(port_status_[PortNumberToIndex(port).value()]);
  EnumerateNext();
}

zx_status_t UsbHubDevice::PowerOnPorts() {
  std::vector<zx_status_t> port_statuses;
  port_statuses.reserve(hub_descriptor_.b_nbr_ports);
  for (uint8_t i = 0; i < hub_descriptor_.b_nbr_ports; i++) {
    port_statuses.push_back(SetFeature(USB_RECIP_PORT, USB_FEATURE_PORT_POWER, i + 1));
  }

  for (auto& status : port_statuses) {
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx::status<usb_port_status_t> UsbHubDevice::GetPortStatus(PortNumber port) {
  zx::status<std::vector<uint8_t>> result = ControlIn(
      USB_RECIP_PORT | USB_DIR_IN, USB_REQ_GET_STATUS, 0, port.value(), sizeof(usb_port_status_t));
  if (result.is_error()) {
    return zx::error(result.error_value());
  }
  auto data = result.value();
  if (data.size() != sizeof(usb_port_status_t)) {
    zxlogf(ERROR, "ERROR: Request response size does not match port_status size");
    return zx::error(ZX_ERR_BAD_STATE);
  }
  usb_port_status_t status;
  memcpy(&status, data.data(), sizeof(status));
  uint16_t port_change = status.w_port_change;
  std::vector<zx_status_t> pending_operations;
  if (port_change & USB_C_PORT_CONNECTION) {
    zxlogf(DEBUG, "USB_C_PORT_CONNECTION ");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_CONNECTION, port.value()));
  }
  if (port_change & USB_C_PORT_ENABLE) {
    zxlogf(DEBUG, "USB_C_PORT_ENABLE ");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_ENABLE, port.value()));
  }
  if (port_change & USB_C_PORT_SUSPEND) {
    zxlogf(DEBUG, "USB_C_PORT_SUSPEND ");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_SUSPEND, port.value()));
  }
  if (port_change & USB_C_PORT_OVER_CURRENT) {
    zxlogf(DEBUG, "USB_C_PORT_OVER_CURRENT ");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_OVER_CURRENT, port.value()));
  }
  if (port_change & USB_C_PORT_RESET) {
    zxlogf(DEBUG, "USB_C_PORT_RESET");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_RESET, port.value()));
  }
  if (port_change & USB_C_BH_PORT_RESET) {
    zxlogf(DEBUG, "USB_C_BH_PORT_RESET");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_BH_PORT_RESET, port.value()));
  }
  if (port_change & USB_C_PORT_LINK_STATE) {
    zxlogf(DEBUG, "USB_C_PORT_LINK_STATE");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_LINK_STATE, port.value()));
  }
  if (port_change & USB_C_PORT_CONFIG_ERROR) {
    zxlogf(DEBUG, "USB_C_PORT_CONFIG_ERROR");
    pending_operations.push_back(
        ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_CONFIG_ERROR, port.value()));
  }
  for (auto& feature_status : pending_operations) {
    if (feature_status != ZX_OK) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }
  return zx::ok(status);
}

void UsbHubDevice::DdkUnbind(ddk::UnbindTxn txn) {
  shutting_down_ = true;
  zx_status_t status = usb_.CancelAll(interrupt_endpoint_.b_endpoint_address);
  if (status != ZX_OK) {
    // Fatal -- unable to shut down properly
    zxlogf(ERROR, "Error %s during CancelAll for interrupt endpoint\n",
           zx_status_get_string(status));
    return;
  }
  // TODO(fxbug.dev/82530): Cancel Control endpoint when supported
  sync_completion_signal(&xfer_done_);
  thrd_join(callback_thread_, &status);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not join interrupt thread");
    return;
  }

  txn.Reply();
}

zx_status_t UsbHubDevice::Bind(std::unique_ptr<fpromise::executor> executor, zx_device_t* parent) {
  auto dev = std::make_unique<UsbHubDevice>(parent, std::move(executor));
  zx_status_t status = dev->Init();
  if (status == ZX_OK) {
    // DDK now owns this pointer.
    auto __UNUSED ref = dev.release();
  }
  return status;
}

zx_status_t UsbHubDevice::SetFeature(uint8_t request_type, uint16_t feature, uint16_t index) {
  return ControlOut(request_type, USB_REQ_SET_FEATURE, feature, index, nullptr, 0);
}

zx_status_t UsbHubDevice::ClearFeature(uint8_t request_type, uint16_t feature, uint16_t index) {
  return ControlOut(request_type, USB_REQ_CLEAR_FEATURE, feature, index, nullptr, 0);
}

zx::status<std::vector<uint8_t>> UsbHubDevice::ControlIn(uint8_t request_type, uint8_t request,
                                                         uint16_t value, uint16_t index,
                                                         size_t read_size) {
  if ((request_type & USB_DIR_MASK) != USB_DIR_IN) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  ZX_ASSERT(read_size <= kMaxRequestLength);
  std::optional<Request> usb_request = AllocRequest();
  usb_request->request()->header.length = read_size;
  usb_request->request()->setup.bm_request_type = request_type;
  usb_request->request()->setup.b_request = request;
  usb_request->request()->setup.w_index = index;
  usb_request->request()->setup.w_value = value;
  usb_request->request()->setup.w_length = static_cast<uint16_t>(read_size);

  std::vector<uint8_t> data;
  zx_status_t status;
  sync_completion_t completion;
  executor_->schedule_task(
      RequestQueue(*std::move(usb_request)).then([&](fpromise::result<Request, void>& value) {
        auto request = std::move(value.take_ok_result().value);
        auto rq_status = request.request()->response.status;
        if (rq_status != ZX_OK) {
          request_pool_.Add(std::move(request));
          status = rq_status;
        } else {
          if (read_size != 0) {
            data.resize(request.request()->response.actual);
            size_t copied = request.CopyFrom(data.data(), data.size(), 0);
            ZX_ASSERT(copied == data.size());
          }
          request_pool_.Add(std::move(request));
        }
        sync_completion_signal(&completion);
      }));
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  return zx::ok(data);
}

zx_status_t UsbHubDevice::ControlOut(uint8_t request_type, uint8_t request, uint16_t value,
                                     uint16_t index, const void* write_buffer, size_t write_size) {
  if ((request_type & USB_DIR_MASK) != USB_DIR_OUT) {
    return ZX_ERR_INVALID_ARGS;
  }
  ZX_ASSERT(write_size <= kMaxRequestLength);
  std::optional<Request> usb_request = AllocRequest();
  usb_request->request()->header.length = write_size;
  usb_request->request()->setup.bm_request_type = request_type;
  usb_request->request()->setup.b_request = request;
  usb_request->request()->setup.w_index = index;
  usb_request->request()->setup.w_value = value;
  usb_request->request()->setup.w_length = static_cast<uint16_t>(write_size);
  size_t result = usb_request->CopyTo(write_buffer, write_size, 0);
  ZX_ASSERT(result == write_size);
  std::atomic<zx_status_t> status = ZX_OK;
  sync_completion_t completion;
  executor_->schedule_task(
      RequestQueue(*std::move(usb_request)).then([&](fpromise::result<Request, void>& value) {
        auto request = std::move(value.take_ok_result().value);
        auto request_status = request.request()->response.status;
        if (request_status != ZX_OK) {
          request_pool_.Add(std::move(request));
          status = request_status;
          return;
        }
        request_pool_.Add(std::move(request));
        sync_completion_signal(&completion);
      }));
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  return ZX_OK;
}

std::optional<Request> UsbHubDevice::AllocRequest() {
  std::optional<Request> request;
  if ((request = request_pool_.Get(kMaxRequestLength))) {
    return request;
  }
  Request::Alloc(&request, kMaxRequestLength, 0, usb_.GetRequestSize());
  ZX_ASSERT(request.has_value());
  return request;
}

fpromise::promise<Request, void> UsbHubDevice::RequestQueue(Request request) {
  fpromise::bridge<Request, void> bridge;
  usb_request_complete_callback_t completion;
  completion.callback = [](void* ctx, usb_request_t* req) {
    std::unique_ptr<fpromise::completer<Request, void>> completer(
        static_cast<fpromise::completer<Request, void>*>(ctx));
    if (req->response.status != ZX_ERR_CANCELED) {
      completer->complete_ok(Request(req, sizeof(usb_request_t)));
    }
  };
  completion.ctx = new fpromise::completer<Request, void>(std::move(bridge.completer));
  usb_.RequestQueue(request.take(), &completion);
  return bridge.consumer.promise().box();
}

zx_status_t UsbHubDevice::Bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<UsbHubDevice>(parent);
  zx_status_t status = dev->Init();
  if (status == ZX_OK) {
    // DDK now owns this pointer.
    auto __UNUSED ref = dev.release();
  }
  return status;
}

void UsbHubDevice::DdkRelease() { delete this; }

UsbHubDevice::~UsbHubDevice() { loop_.Shutdown(); }

}  // namespace usb_hub
static zx_driver_ops_t usb_hub_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hub::UsbHubDevice::Bind,
};

ZIRCON_DRIVER(usb_hub_rewrite, usb_hub_driver_ops, "fuchsia", "0.1");
