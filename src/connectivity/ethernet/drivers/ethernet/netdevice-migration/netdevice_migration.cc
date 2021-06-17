// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdevice_migration.h"

#include <lib/ddk/debug.h>
#include <zircon/system/public/zircon/assert.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "src/connectivity/ethernet/drivers/ethernet/netdevice-migration/netdevice_migration_bind.h"

namespace netdevice_migration {

zx_status_t NetdeviceMigration::Bind(void* ctx, zx_device_t* dev) {
  fbl::AllocChecker ac;
  auto netdevm = std::unique_ptr<NetdeviceMigration>(new (&ac) NetdeviceMigration(dev));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (zx_status_t status = netdevm->Init(); status != ZX_OK) {
    return status;
  }

  // On a successful call to Bind(), Devmgr takes ownership of the driver, which it releases by
  // calling DdkRelease(). Consequently, we transfer our ownership to a local and let it drop.
  auto __UNUSED temp_ref = netdevm.release();
  return ZX_OK;
}

zx_status_t NetdeviceMigration::Init() {
  if (!ethernet_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (zx_status_t status = ethernet_.Query(0, &eth_info_); status != ZX_OK) {
    zxlogf(ERROR, "netdevice-migration: failed to query parent: %s\n",
           zx_status_get_string(status));
    return status;
  }

  ethernet_.GetBti(&eth_bti_);

  if (zx_status_t status = DdkAdd(
          ddk::DeviceAddArgs("netdevice-migration").set_proto_id(ZX_PROTOCOL_NETWORK_DEVICE_IMPL));
      status != ZX_OK) {
    zxlogf(ERROR, "netdevice-migration: failed to bind: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void NetdeviceMigration::DdkRelease() { delete this; }

void NetdeviceMigration::EthernetIfcStatus(uint32_t status) {}

void NetdeviceMigration::EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size,
                                         uint32_t flags) {}

zx_status_t NetdeviceMigration::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  if (netdevice_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  netdevice_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  return ZX_OK;
}

void NetdeviceMigration::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                                void* cookie) {
  {
    fbl::AutoLock lock(&lock_);
    if (started_) {
      zxlogf(WARNING, "netdevice-migration: device already started\n");
      callback(cookie);
      return;
    }
  }
  zx_status_t status = ethernet_.Start(this, &ethernet_ifc_protocol_ops_);
  // TODO(https://fxbug.dev/78873): Return error status once NetworkDeviceImplStart is fallible.
  ZX_ASSERT_MSG(status == ZX_OK, "netdevice-migration: failed to start device: %s\n",
                zx_status_get_string(status));
  {
    fbl::AutoLock lock(&lock_);
    started_ = true;
  }
  callback(cookie);
}

void NetdeviceMigration::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                               void* cookie) {
  ethernet_.Stop();
  {
    fbl::AutoLock lock(&lock_);
    started_ = false;
  }
  callback(cookie);
}

void NetdeviceMigration::NetworkDeviceImplGetInfo(device_info_t* out_info) {}

void NetdeviceMigration::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list,
                                                  size_t buffers_count) {}

void NetdeviceMigration::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                                       size_t buffers_count) {}

void NetdeviceMigration::NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo) {}

void NetdeviceMigration::NetworkDeviceImplReleaseVmo(uint8_t id) {}

void NetdeviceMigration::NetworkDeviceImplSetSnoop(bool snoop) {}

bool NetdeviceMigration::IsStarted() {
  fbl::AutoLock lock(&lock_);
  return started_;
}

static zx_driver_ops_t netdevice_migration_driver_ops = []() -> zx_driver_ops_t {
  return {
      .version = DRIVER_OPS_VERSION,
      .bind = NetdeviceMigration::Bind,
  };
}();

}  // namespace netdevice_migration

ZIRCON_DRIVER(NetdeviceMigration, netdevice_migration::netdevice_migration_driver_ops, "zircon",
              "0.1");
