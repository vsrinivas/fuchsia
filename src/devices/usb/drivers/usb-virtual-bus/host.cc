// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/usb/drivers/usb-virtual-bus/host.h"

#include <fcntl.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/hw/usb.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/devices/usb/drivers/usb-virtual-bus/usb-virtual-bus-tester-bind.h"

namespace virtualbus {

void Device::RequestComplete(usb_request_t* request) {
  usb::Request<> req(request, parent_req_size_);
  constexpr auto kExpected = 20;
  completer_->Reply(req.request()->response.actual == kExpected);
}

Device::~Device() {}

void Device::DdkUnbind(ddk::UnbindTxn txn) {
  cancel_thread_ = std::thread([this, unbind_txn = std::move(txn)]() mutable {
    usb_client_.CancelAll(bulk_out_addr_);
    unbind_txn.Reply();
  });
}

void Device::RunShortPacketTest(RunShortPacketTestCompleter::Sync& completer) {
  if (completer_.has_value()) {
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }
  usb_request_complete_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<Device*>(ctx)->RequestComplete(request);
          },
      .ctx = this,
  };
  completer_ = completer.ToAsync();
  std::optional<usb::Request<>> req;
  constexpr auto kUsbBufSize = 100;
  usb::Request<>::Alloc(&req, kUsbBufSize, bulk_out_addr_, parent_req_size_);
  usb_client_.RequestQueue(req->take(), &complete);
}

void Device::DdkRelease() {
  cancel_thread_.join();
  delete this;
}

zx_status_t Device::Bind() {
  zx_status_t status = ZX_OK;

  if (!usb_client_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Find our endpoints.
  std::optional<usb::InterfaceList> usb_interface_list;
  status = usb::InterfaceList::Create(usb_client_, true, &usb_interface_list);
  if (status != ZX_OK) {
    return status;
  }

  uint8_t bulk_out_addr = 0;

  for (auto& interface : *usb_interface_list) {
    for (auto ep_itr : interface.GetEndpointList()) {
      if (usb_ep_direction(&ep_itr.descriptor) == USB_ENDPOINT_OUT) {
        if (usb_ep_type(&ep_itr.descriptor) == USB_ENDPOINT_BULK) {
          bulk_out_addr = ep_itr.descriptor.bEndpointAddress;
        }
      }
    }
  }

  if (!bulk_out_addr) {
    zxlogf(ERROR, "could not find bulk out endpoint");
    return ZX_ERR_NOT_SUPPORTED;
  }

  parent_req_size_ = usb_client_.GetRequestSize();

  status = DdkAdd("virtual-bus-test");
  if (status != ZX_OK) {
    zxlogf(ERROR, "device_add failed");
    return status;
  }

  bulk_out_addr_ = bulk_out_addr;

  return status;
}

zx_status_t Bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<virtualbus::Device>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // Devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

}  // namespace virtualbus

namespace {

static constexpr zx_driver_ops_t virtualbustest_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtualbus::Bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(virtualbustest, virtualbustest_driver_ops, "zircon", "0.1");
