// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_device.h"

#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

namespace network {

NetworkDevice::~NetworkDevice() {
  if (loop_thread_.has_value()) {
    // not allowed to destroy device on the loop thread, will cause deadlock
    ZX_ASSERT(loop_thread_.value() != thrd_current());
  }
  loop_.Shutdown();
}

zx_status_t NetworkDevice::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto netdev = fbl::make_unique_checked<NetworkDevice>(&ac, parent);

  if (!ac.check()) {
    zxlogf(ERROR, "network-device: No memory");
    return ZX_ERR_NO_MEMORY;
  }

  thrd_t thread;
  zx_status_t status;
  if ((status = netdev->loop_.StartThread("network-device-handler", &thread)) != ZX_OK) {
    zxlogf(ERROR, "network-device: Failed to create handler thread");
    return status;
  }
  netdev->loop_thread_ = thread;

  ddk::NetworkDeviceImplProtocolClient netdevice_impl(parent);
  if (!netdevice_impl.is_valid()) {
    zxlogf(ERROR, "network-device: Bind failed, protocol not available");
    return ZX_ERR_NOT_FOUND;
  }

  if ((status = NetworkDeviceInterface::Create(netdev->loop_.dispatcher(), netdevice_impl,
                                               device_get_name(parent), &netdev->device_)) !=
      ZX_OK) {
    zxlogf(ERROR, "network-device: Failed to create inner device %s",
           zx_status_get_string(status));
    return status;
  }

  // If our parent supports the MacAddrImpl protocol, create the handler for it.
  ddk::MacAddrImplProtocolClient mac_impl(parent);
  if (mac_impl.is_valid()) {
    if ((status = MacAddrDeviceInterface::Create(mac_impl, &netdev->mac_)) != ZX_OK) {
      zxlogf(ERROR, "network-device: Failed to create inner mac device: %s",
             zx_status_get_string(status));
      return status;
    }
  }

  if ((status = netdev->DdkAdd(
           ddk::DeviceAddArgs("network-device").set_proto_id(ZX_PROTOCOL_NETWORK_DEVICE))) !=
      ZX_OK) {
    zxlogf(ERROR, "network-device: Failed to bind %d", status);
    return status;
  }

  // On successful Add, Devmgr takes ownership (relinquished on DdkRelease),
  // so transfer our ownership to a local var, and let it go out of scope.
  auto __UNUSED temp_ref = netdev.release();

  return ZX_OK;
}

zx_status_t NetworkDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  ::llcpp::fuchsia::hardware::network::DeviceInstance::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void NetworkDevice::DdkUnbind(ddk::UnbindTxn unbindTxn) {
  zxlogf(DEBUG, "network-device: DdkUnbind");
  device_->Teardown([this, txn = std::move(unbindTxn)]() mutable {
    if (mac_) {
      mac_->Teardown([txn = std::move(txn)]() mutable { txn.Reply(); });
    } else {
      txn.Reply();
    }
  });
}

void NetworkDevice::DdkRelease() {
  zxlogf(DEBUG, "network-device: DdkRelease");
  delete this;
}

void NetworkDevice::GetDevice(zx::channel device, GetDeviceCompleter::Sync _completer) {
  ZX_ASSERT_MSG(device_, "Can't serve device if not bound to parent implementation");
  device_->Bind(std::move(device));
}

void NetworkDevice::GetMacAddressing(zx::channel mac, GetMacAddressingCompleter::Sync _completer) {
  if (mac_) {
    mac_->Bind(loop_.dispatcher(), std::move(mac));
  }
}

static zx_driver_ops_t network_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NetworkDevice::Create;
  return ops;
}();

}  // namespace network

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(network, network::network_driver_ops, "zircon", "0.1", 1)
            BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_NETWORK_DEVICE_IMPL),
ZIRCON_DRIVER_END(network)
    // clang-format on
