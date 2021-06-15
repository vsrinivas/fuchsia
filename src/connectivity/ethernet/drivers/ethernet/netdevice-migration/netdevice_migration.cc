// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdevice_migration.h"

#include "src/connectivity/ethernet/drivers/ethernet/netdevice-migration/netdevice_migration_bind.h"

namespace netdevice_migration {

zx_status_t NetdeviceMigration::Bind(void* ctx, zx_device_t* dev) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t NetdeviceMigration::Add() { return DdkAdd(ddk::DeviceAddArgs("netdevice_migration")); }

void NetdeviceMigration::DdkRelease() { delete this; }

void NetdeviceMigration::EthernetIfcStatus(uint32_t status) {}

void NetdeviceMigration::EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size,
                                         uint32_t flags) {}

zx_status_t NetdeviceMigration::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  return ZX_ERR_NOT_SUPPORTED;
}

void NetdeviceMigration::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                                void* cookie) {}

void NetdeviceMigration::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                               void* cookie) {}

void NetdeviceMigration::NetworkDeviceImplGetInfo(device_info_t* out_info) {}

void NetdeviceMigration::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list,
                                                  size_t buffers_count) {}

void NetdeviceMigration::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                                       size_t buffers_count) {}

void NetdeviceMigration::NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo) {}

void NetdeviceMigration::NetworkDeviceImplReleaseVmo(uint8_t id) {}

void NetdeviceMigration::NetworkDeviceImplSetSnoop(bool snoop) {}

static zx_driver_ops_t netdevice_migration_driver_ops = []() -> zx_driver_ops_t {
  return {
      .version = DRIVER_OPS_VERSION,
      .bind = NetdeviceMigration::Bind,
  };
}();

}  // namespace netdevice_migration

ZIRCON_DRIVER(NetdeviceMigration, netdevice_migration::netdevice_migration_driver_ops, "zircon",
              "0.1");
