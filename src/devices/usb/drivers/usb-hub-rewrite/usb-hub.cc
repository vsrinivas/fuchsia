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
// Returns an empty promise.
static fit::promise<void, void> EmptyPromise() { return fit::make_ok_promise(); }
}  // namespace

namespace usb_hub {
zx_status_t UsbHubDevice::Init() {
  zx_status_t status =
      DdkAdd(ddk::DeviceAddArgs("usb-hub").set_inspect_vmo(inspector_.DuplicateVmo()));
  if (status != ZX_OK) {
    return status;
  }
  usb_ = ddk::UsbProtocolClient(parent());
  bus_ = ddk::UsbBusProtocolClient(parent());
  return status;
}

zx_status_t UsbHubDevice::UsbHubInterfaceResetPort(uint32_t port) { return ZX_ERR_NOT_SUPPORTED; }

void UsbHubDevice::DdkInit(ddk::InitTxn txn) {
  // First -- configure all endpoints and initialize the hub interface
  zx_status_t status = ddk_interaction_loop_.StartThread();
  if (status != ZX_OK) {
    txn.Reply(status);
    return;
  }
  ddk_interaction_executor_.schedule_task(
      EmptyPromise().and_then([this, init_txn = std::move(txn)]() mutable {
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

UsbHubDevice::~UsbHubDevice() { ddk_interaction_loop_.Shutdown(); }

}  // namespace usb_hub
static zx_driver_ops_t usb_hub_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hub::UsbHubDevice::Bind,
};

ZIRCON_DRIVER(usb_hub_rewrite, usb_hub_driver_ops, "fuchsia", "0.1")
