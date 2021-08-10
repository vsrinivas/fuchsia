// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdevice_migration.h"

#include <fuchsia/hardware/ethernet/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <zircon/system/public/zircon/assert.h>

#include <fbl/alloc_checker.h>

#include "src/connectivity/ethernet/drivers/ethernet/netdevice-migration/netdevice_migration_bind.h"

namespace {

fuchsia_hardware_network::wire::StatusFlags ToStatusFlags(uint32_t ethernet_status) {
  if (ethernet_status ==
      static_cast<uint32_t>(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline)) {
    return fuchsia_hardware_network::wire::StatusFlags::kOnline;
  } else {
    return fuchsia_hardware_network::wire::StatusFlags();
  }
}

}  // namespace

namespace netdevice_migration {

zx_status_t NetdeviceMigration::Bind(void* ctx, zx_device_t* dev) {
  zx::status netdevm_result = Create(dev);
  if (netdevm_result.is_error()) {
    return netdevm_result.error_value();
  }
  auto& netdevm = netdevm_result.value();
  if (zx_status_t status = netdevm->DeviceAdd(); status != ZX_OK) {
    zxlogf(ERROR, "netdevice-migration: failed to bind: %s\n", zx_status_get_string(status));
    return status;
  }
  // On a successful call to Bind(), Devmgr takes ownership of the driver, which it releases by
  // calling DdkRelease(). Consequently, we transfer our ownership to a local and let it drop.
  auto __UNUSED temp_ref = netdevm.release();
  return ZX_OK;
}

zx::status<std::unique_ptr<NetdeviceMigration>> NetdeviceMigration::Create(zx_device_t* dev) {
  ddk::EthernetImplProtocolClient ethernet(dev);
  if (!ethernet.is_valid()) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  vmo_store::Options opts = {
      .map =
          vmo_store::MapOptions{
              .vm_option = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
              .vmar = nullptr,
          },
  };
  ethernet_info_t eth_info;
  if (zx_status_t status = ethernet.Query(0, &eth_info); status != ZX_OK) {
    zxlogf(ERROR, "netdevice-migration: failed to query parent: %s\n",
           zx_status_get_string(status));
    return zx::error(status);
  }
  zx::bti eth_bti;
  if (eth_info.features & ETHERNET_FEATURE_DMA) {
    ethernet.GetBti(&eth_bti);
    if (!eth_bti.is_valid()) {
      zxlogf(ERROR, "netdevice-migration: failed to get valid bti handle");
      return zx::error(ZX_ERR_BAD_HANDLE);
    }
    opts.pin = vmo_store::PinOptions{
        .bti = eth_bti.borrow(),
        .bti_pin_options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
        .index = true,
    };
  }
  fbl::AllocChecker ac;
  auto netdevm = std::unique_ptr<NetdeviceMigration>(
      new (&ac) NetdeviceMigration(dev, ethernet, std::move(eth_bti), opts));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  {
    fbl::AutoLock vmo_lock(&netdevm->vmo_lock_);
    if (zx_status_t status = netdevm->vmo_store_.Reserve(MAX_VMOS); status != ZX_OK) {
      zxlogf(ERROR, "netdevice-migration: failed to initialize vmo store: %s",
             zx_status_get_string(status));
      return zx::error(status);
    }
  }

  return zx::ok(std::move(netdevm));
}

zx_status_t NetdeviceMigration::DeviceAdd() {
  return DdkAdd(
      ddk::DeviceAddArgs("netdevice-migration").set_proto_id(ZX_PROTOCOL_NETWORK_DEVICE_IMPL));
}

void NetdeviceMigration::DdkRelease() { delete this; }

void NetdeviceMigration::EthernetIfcStatus(uint32_t status) __TA_EXCLUDES(lock_) {
  port_status_t port_status = {
      .mtu = ETH_MTU_SIZE,
  };
  {
    fbl::AutoLock lock(&lock_);
    port_status_flags_ = ToStatusFlags(status);
    port_status.flags = status;
  }
  netdevice_.PortStatusChanged(kPortId, &port_status);
}

void NetdeviceMigration::EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size,
                                         uint32_t flags) {}

zx_status_t NetdeviceMigration::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  if (netdevice_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  netdevice_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  netdevice_.AddPort(kPortId, this, &network_port_protocol_ops_);
  return ZX_OK;
}

void NetdeviceMigration::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                                void* cookie) __TA_EXCLUDES(lock_) {
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
                                               void* cookie) __TA_EXCLUDES(lock_) {
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

void NetdeviceMigration::NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo)
    __TA_EXCLUDES(vmo_lock_) {
  fbl::AutoLock vmo_lock(&vmo_lock_);
  if (zx_status_t status = vmo_store_.RegisterWithKey(id, std::move(vmo)); status != ZX_OK) {
    zxlogf(ERROR, "netdevice-migration: failed to prepare vmo id = %d: %s", id,
           zx_status_get_string(status));
    // Remove the driver because a failure to register the vmo indicates that the driver will not be
    // able to perform tx/rx.
    DdkAsyncRemove();
  }
}

void NetdeviceMigration::NetworkDeviceImplReleaseVmo(uint8_t id) __TA_EXCLUDES(vmo_lock_) {
  fbl::AutoLock vmo_lock(&vmo_lock_);
  if (zx::status<zx::vmo> status = vmo_store_.Unregister(id); status.status_value() != ZX_OK) {
    // A failure here may be the result of a failed call to register the vmo, in which case the
    // driver is queued for removal from device manager. Accordingly, we must not panic lest we
    // disrupt the orderly shutdown of the driver: a log statement is the best we can do.
    zxlogf(ERROR, "netdevice-migration: failed to release vmo id = %d: %s", id,
           status.status_string());
  }
}

void NetdeviceMigration::NetworkDeviceImplSetSnoop(bool snoop) {}

void NetdeviceMigration::NetworkPortGetInfo(port_info_t* out_info) { *out_info = {}; }

void NetdeviceMigration::NetworkPortGetStatus(port_status_t* out_status) __TA_EXCLUDES(lock_) {
  fbl::AutoLock lock(&lock_);
  *out_status = {
      .mtu = ETH_MTU_SIZE,
      .flags = static_cast<uint32_t>(port_status_flags_),
  };
}

void NetdeviceMigration::NetworkPortSetActive(bool active) {}

void NetdeviceMigration::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) { *out_mac_ifc = {}; }

void NetdeviceMigration::NetworkPortRemoved() {}

bool NetdeviceMigration::IsStarted() __TA_EXCLUDES(lock_) {
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
