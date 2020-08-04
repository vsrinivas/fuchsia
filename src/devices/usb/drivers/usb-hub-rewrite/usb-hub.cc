// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hub.h"

#include <lib/sync/completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/hw/usb/hub.h>
#include <zircon/listnode.h>
#include <zircon/status.h>

#include <ddk/binding.h>

#include "src/devices/usb/drivers/usb-hub-rewrite/usb_hub_rewrite_bind.h"

namespace {
// Collapses a vector of promises into a single promise which returns a user-provided value on
// success, and an error code on failure.
template <typename Success, typename Error, typename ReturnType>
fit::promise<ReturnType, Error> Fold(fit::promise<std::vector<fit::result<Success, Error>>> promise,
                                     ReturnType ok_value) {
  return promise.then([success_value = std::move(ok_value)](
                          fit::result<std::vector<fit::result<void, zx_status_t>>, void>& results)
                          -> fit::result<ReturnType, Error> {
    for (auto& result : results.value()) {
      if (result.is_error()) {
        return fit::error(result.error());
      }
    }
    return fit::ok(success_value);
  });
}
}  // namespace

namespace usb_hub {
zx_status_t UsbHubDevice::Init() {
  usb_ = ddk::UsbProtocolClient(parent());
  bus_ = ddk::UsbBusProtocolClient(parent());
  zx_status_t status =
      DdkAdd(ddk::DeviceAddArgs("usb-hub").set_inspect_vmo(inspector_.DuplicateVmo()));

  return status;
}

zx_status_t UsbHubDevice::UsbHubInterfaceResetPort(uint32_t port) { return ZX_ERR_NOT_SUPPORTED; }

void UsbHubDevice::DdkInit(ddk::InitTxn txn) {
  // First -- configure all endpoints and initialize the hub interface
  zx_status_t status = loop_.StartThread();
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }
  status = loop_.StartThread();
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }
  blocking_executor_.schedule_task(fit::make_promise([this, init_txn = std::move(txn)]() mutable {
    std::optional<usb::InterfaceList> interfaces;
    usb::InterfaceList::Create(usb_, false, &interfaces);
    zx_status_t status = ZX_ERR_IO;
    // According to USB 2.0 Specification section 11.12.1 a hub should have exactly one
    // interrupt endpoint and no other endpoints.
    for (auto& interface : *interfaces) {
      if (interface.descriptor()->bNumEndpoints == 1) {
        auto eplist = interface.GetEndpointList();
        auto ep_iter = eplist.begin();
        auto ep = ep_iter.endpoint();
        interrupt_endpoint_ = ep->descriptor;
        if (ep->has_companion) {
          status = usb_.EnableEndpoint(&interrupt_endpoint_, &ep->ss_companion, true);
        } else {
          status = usb_.EnableEndpoint(&interrupt_endpoint_, nullptr, true);
        }
        break;
      }
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "Initialization failed due to %s", zx_status_get_string(status));
      init_txn.Reply(status);
    }
    speed_ = usb_.GetSpeed();
    init_txn.Reply(ZX_OK);
  }));
}

zx_status_t UsbHubDevice::RunSynchronously(fit::promise<void, zx_status_t> promise) {
  sync_completion_t completion;
  zx_status_t status;
  bool complete = false;
  executor_.schedule_task(promise.then([&](fit::result<void, zx_status_t>& result) {
    status = ZX_OK;
    if (result.is_error()) {
      status = result.error();
    }
    complete = true;
    sync_completion_signal(&completion);
  }));
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  ZX_ASSERT(complete);
  return status;
}

fit::promise<usb_port_status_t, zx_status_t> UsbHubDevice::GetPortStatus(uint8_t port) {
  return ControlIn(USB_RECIP_PORT | USB_DIR_IN, USB_REQ_GET_STATUS, 0, port,
                   sizeof(usb_port_status_t))
      .and_then([](std::vector<uint8_t>& data) -> fit::result<usb_port_status_t, zx_status_t> {
        if (data.size() != sizeof(usb_port_status_t)) {
          return fit::error(ZX_ERR_IO);
        }
        usb_port_status_t status;
        memcpy(&status, data.data(), sizeof(status));
        return fit::ok(status);
      })
      .and_then([this, port](usb_port_status_t& status) {
        uint16_t port_change = status.wPortChange;
        std::vector<fit::promise<void, zx_status_t>> pending_operations;
        if (port_change & USB_C_PORT_CONNECTION) {
          zxlogf(DEBUG, "USB_C_PORT_CONNECTION ");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_CONNECTION, port));
        }
        if (port_change & USB_C_PORT_ENABLE) {
          zxlogf(DEBUG, "USB_C_PORT_ENABLE ");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_ENABLE, port));
        }
        if (port_change & USB_C_PORT_SUSPEND) {
          zxlogf(DEBUG, "USB_C_PORT_SUSPEND ");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_SUSPEND, port));
        }
        if (port_change & USB_C_PORT_OVER_CURRENT) {
          zxlogf(DEBUG, "USB_C_PORT_OVER_CURRENT ");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_OVER_CURRENT, port));
        }
        if (port_change & USB_C_PORT_RESET) {
          zxlogf(DEBUG, "USB_C_PORT_RESET");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_RESET, port));
        }
        if (port_change & USB_C_BH_PORT_RESET) {
          zxlogf(DEBUG, "USB_C_BH_PORT_RESET");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_BH_PORT_RESET, port));
        }
        if (port_change & USB_C_PORT_LINK_STATE) {
          zxlogf(DEBUG, "USB_C_PORT_LINK_STATE");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_LINK_STATE, port));
        }
        if (port_change & USB_C_PORT_CONFIG_ERROR) {
          zxlogf(DEBUG, "USB_C_PORT_CONFIG_ERROR");
          pending_operations.push_back(
              ClearFeature(USB_RECIP_PORT, USB_FEATURE_C_PORT_CONFIG_ERROR, port));
        }
        return Fold(fit::join_promise_vector(std::move(pending_operations)).box(), status);
      });
}

fit::promise<void, zx_status_t> UsbHubDevice::Sleep(zx::time deadline) {
  fit::bridge<void, zx_status_t> bridge;
  zx_status_t status = async::PostTaskForTime(
      loop_.dispatcher(),
      [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); }, deadline);
  if (status != ZX_OK) {
    return fit::make_error_promise(status);
  }
  return bridge.consumer.promise().box();
}

fit::promise<void, zx_status_t> UsbHubDevice::SetFeature(uint8_t request_type, uint16_t feature,
                                                         uint16_t index) {
  return ControlOut(request_type, USB_REQ_SET_FEATURE, feature, index, nullptr, 0);
}

fit::promise<void, zx_status_t> UsbHubDevice::ClearFeature(uint8_t request_type, uint16_t feature,
                                                           uint16_t index) {
  return ControlOut(request_type, USB_REQ_CLEAR_FEATURE, feature, index, nullptr, 0);
}

fit::promise<std::vector<uint8_t>, zx_status_t> UsbHubDevice::ControlIn(
    uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, size_t read_size) {
  if ((request_type & USB_DIR_MASK) != USB_DIR_IN) {
    return fit::make_result_promise<std::vector<uint8_t>, zx_status_t>(
        fit::error(ZX_ERR_INVALID_ARGS));
  }
  ZX_ASSERT(read_size <= kMaxRequestLength);
  std::optional<Request> usb_request = AllocRequest();
  usb_request->request()->header.length = read_size;
  usb_request->request()->setup.bmRequestType = request_type;
  usb_request->request()->setup.bRequest = request;
  usb_request->request()->setup.wIndex = index;
  usb_request->request()->setup.wValue = value;
  usb_request->request()->setup.wLength = static_cast<uint16_t>(read_size);

  return RequestQueue(*std::move(usb_request))
      .then([this, read_size](fit::result<Request, void>& value)
                -> fit::result<std::vector<uint8_t>, zx_status_t> {
        auto request = std::move(value.take_ok_result().value);
        auto status = request.request()->response.status;
        if (status != ZX_OK) {
          request_pool_.Add(std::move(request));
          return fit::error(status);
        }
        std::vector<uint8_t> data;
        if (read_size != 0) {
          data.resize(request.request()->response.actual);
          request.CopyFrom(data.data(), data.size(), 0);
        }
        request_pool_.Add(std::move(request));
        return fit::ok(data);
      })
      .box();
}

fit::promise<void, zx_status_t> UsbHubDevice::ControlOut(uint8_t request_type, uint8_t request,
                                                         uint16_t value, uint16_t index,
                                                         const void* write_buffer,
                                                         size_t write_size) {
  if ((request_type & USB_DIR_MASK) != USB_DIR_OUT) {
    return fit::make_result_promise<void, zx_status_t>(fit::error(ZX_ERR_INVALID_ARGS));
  }
  ZX_ASSERT(write_size <= kMaxRequestLength);
  std::optional<Request> usb_request = AllocRequest();
  usb_request->request()->header.length = write_size;
  usb_request->request()->setup.bmRequestType = request_type;
  usb_request->request()->setup.bRequest = request;
  usb_request->request()->setup.wIndex = index;
  usb_request->request()->setup.wValue = value;
  usb_request->request()->setup.wLength = static_cast<uint16_t>(write_size);
  usb_request->CopyTo(write_buffer, write_size, 0);
  return RequestQueue(*std::move(usb_request))
      .then([this](fit::result<Request, void>& value) -> fit::result<void, zx_status_t> {
        auto request = std::move(value.take_ok_result().value);
        auto status = request.request()->response.status;
        if (status != ZX_OK) {
          request_pool_.Add(std::move(request));
          return fit::error(status);
        }
        request_pool_.Add(std::move(request));
        return fit::ok();
      })
      .box();
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

fit::promise<Request, void> UsbHubDevice::RequestQueue(Request request) {
  fit::bridge<Request, void> bridge;
  usb_request_complete_t completion;
  completion.callback = [](void* ctx, usb_request_t* req) {
    std::unique_ptr<fit::completer<Request, void>> completer(
        static_cast<fit::completer<Request, void>*>(ctx));
    if (req->response.status != ZX_ERR_CANCELED) {
      completer->complete_ok(Request(req, sizeof(usb_request_t)));
    }
  };
  completion.ctx = new fit::completer<Request, void>(std::move(bridge.completer));
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

void UsbHubDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void UsbHubDevice::DdkRelease() { delete this; }

UsbHubDevice::~UsbHubDevice() { loop_.Shutdown(); }

}  // namespace usb_hub
static zx_driver_ops_t usb_hub_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hub::UsbHubDevice::Bind,
};

ZIRCON_DRIVER(usb_hub_rewrite, usb_hub_driver_ops, "fuchsia", "0.1")
