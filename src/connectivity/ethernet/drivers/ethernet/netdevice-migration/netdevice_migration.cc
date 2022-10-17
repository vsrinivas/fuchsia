// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdevice_migration.h"

#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/zircon-internal/align.h>
#include <zircon/system/public/zircon/assert.h>

#include <algorithm>

#include <fbl/alloc_checker.h>

#include "src/connectivity/ethernet/drivers/ethernet/netdevice-migration/netdevice_migration_bind.h"

namespace {

fuchsia_hardware_network::wire::StatusFlags ToStatusFlags(uint32_t ethernet_status) {
  fuchsia_hardware_network::wire::StatusFlags flags;
  if (fuchsia_hardware_ethernet::wire::DeviceStatus(ethernet_status) &
      fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline) {
    flags |= fuchsia_hardware_network::wire::StatusFlags::kOnline;
  }
  return flags;
}

}  // namespace

namespace netdevice_migration {

zx_status_t NetdeviceMigration::Bind(void* ctx, zx_device_t* dev) {
  zx::result netdevm_result = Create(dev);
  if (netdevm_result.is_error()) {
    return netdevm_result.error_value();
  }
  auto& netdevm = netdevm_result.value();
  if (zx_status_t status = netdevm->DeviceAdd(); status != ZX_OK) {
    zxlogf(ERROR, "failed to bind: %s", zx_status_get_string(status));
    return status;
  }
  // On a successful call to Bind(), Devmgr takes ownership of the driver, which it releases by
  // calling DdkRelease(). Consequently, we transfer our ownership to a local and let it drop.
  auto __UNUSED temp_ref = netdevm.release();
  return ZX_OK;
}

zx::result<std::unique_ptr<NetdeviceMigration>> NetdeviceMigration::Create(zx_device_t* dev) {
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
    zxlogf(ERROR, "failed to query parent: %s", zx_status_get_string(status));
    return zx::error(status);
  }
  zx::bti eth_bti;
  if (eth_info.features & ETHERNET_FEATURE_DMA) {
    ethernet.GetBti(&eth_bti);
    if (!eth_bti.is_valid()) {
      zxlogf(ERROR, "failed to get valid bti handle");
      return zx::error(ZX_ERR_BAD_HANDLE);
    }
    opts.pin = vmo_store::PinOptions{
        .bti = eth_bti.borrow(),
        .bti_pin_options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
        .index = true,
    };
  }
  std::array<uint8_t, sizeof(eth_info.mac)> mac;
  std::copy_n(eth_info.mac, sizeof(eth_info.mac), mac.begin());
  if (eth_info.netbuf_size < sizeof(ethernet_netbuf_t)) {
    zxlogf(ERROR, "invalid buffer size %ld < min %zu", eth_info.netbuf_size,
           sizeof(ethernet_netbuf_t));
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  eth_info.netbuf_size = ZX_ROUNDUP(eth_info.netbuf_size, 8);
  NetbufPool netbuf_pool;
  for (uint32_t i = 0; i < kFifoDepth; i++) {
    std::optional netbuf = Netbuf::Alloc(eth_info.netbuf_size);
    if (!netbuf.has_value()) {
      return zx::error(ZX_ERR_NO_MEMORY);
    }
    netbuf_pool.push(std::move(netbuf.value()));
  }
  fbl::AllocChecker ac;
  auto netdevm = std::unique_ptr<NetdeviceMigration>(
      new (&ac) NetdeviceMigration(dev, ethernet, eth_info.mtu, std::move(eth_bti), opts, mac,
                                   eth_info.netbuf_size, std::move(netbuf_pool)));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  {
    fbl::AutoLock vmo_lock(&netdevm->vmo_lock_);
    if (zx_status_t status = netdevm->vmo_store_.Reserve(MAX_VMOS); status != ZX_OK) {
      zxlogf(ERROR, "failed to initialize vmo store: %s", zx_status_get_string(status));
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

void NetdeviceMigration::EthernetIfcStatus(uint32_t status) __TA_EXCLUDES(status_lock_) {
  port_status_t port_status = {
      .mtu = mtu_,
  };
  {
    std::lock_guard lock(status_lock_);
    port_status_flags_ = ToStatusFlags(status);
    port_status.flags = status;
  }
  netdevice_.PortStatusChanged(kPortId, &port_status);
}

void NetdeviceMigration::EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size,
                                         uint32_t flags) __TA_EXCLUDES(rx_lock_, vmo_lock_) {
  rx_space_buffer_t space;
  // Use a closure to move logging outside of the scope of the lock.
  const zx_status_t status = [&]() {
    std::lock_guard rx_lock(rx_lock_);
    if (rx_spaces_.empty()) {
      return ZX_ERR_NO_RESOURCES;
    }
    space = rx_spaces_.front();
    rx_spaces_.pop();
    // Bounds check the incoming frame to verify that the ethernet driver respects the MTU.
    if (data_size > space.region.length) {
      DdkAsyncRemove();
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    {
      network::SharedAutoLock vmo_lock(&vmo_lock_);
      auto* vmo = vmo_store_.GetVmo(space.region.vmo);
      if (vmo == nullptr) {
        DdkAsyncRemove();
        return ZX_ERR_INVALID_ARGS;
      }
      cpp20::span<uint8_t> vmo_view = vmo->data();
      std::copy_n(data_buffer, data_size, vmo_view.begin() + space.region.offset);
    }
    rx_buffer_part_t part = {
        .id = space.id,
        .offset = 0,
        .length = static_cast<uint32_t>(data_size),
    };
    rx_buffer_t buf = {
        .meta =
            {
                .port = kPortId,
                .frame_type = static_cast<uint8_t>(fuchsia_hardware_network::FrameType::kEthernet),
            },
        .data_list = &part,
        .data_count = 1,
    };
    netdevice_.CompleteRx(&buf, 1);
    return ZX_OK;
  }();

  switch (status) {
    case ZX_OK:
      break;
    case ZX_ERR_NO_RESOURCES: {
      constexpr size_t N = 64;
      // Assert power of 2 to avoid incorrect behavior on overflow.
      static_assert(N != 0 && (N & (N - 1)) == 0);
      // Use post-increment to ensure we log on the first dropped packet.
      const size_t v = no_rx_space_++;
      if (v % N == 0) {
        zxlogf(ERROR, "received ethernet frames without queued rx buffers; %zu frames dropped",
               v + 1);
      }
    } break;
    case ZX_ERR_BUFFER_TOO_SMALL:
      zxlogf(ERROR, "received ethernet frames larger than rx buffer length of %lu",
             space.region.length);
      break;
    case ZX_ERR_INVALID_ARGS:
      zxlogf(ERROR, "queued frames with unknown VMO IDs");
      break;
    default:
      ZX_PANIC("unexpected status %s", zx_status_get_string(status));
      break;
  }
}

zx_status_t NetdeviceMigration::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  if (netdevice_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  netdevice_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  netdevice_.AddPort(kPortId, this, &network_port_protocol_ops_);
  return ZX_OK;
}

void NetdeviceMigration::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                                void* cookie) __TA_EXCLUDES(rx_lock_, tx_lock_) {
  {
    std::lock_guard rx_lock(rx_lock_);
    std::lock_guard tx_lock(tx_lock_);
    if (tx_started_ || rx_started_) {
      zxlogf(WARNING, "device already started");
      callback(cookie, ZX_ERR_ALREADY_BOUND);
      return;
    }
  }
  // Do not hold the lock across the ethernet_.Start() call because the Netdevice contract ensures
  // that a subsequent Start() or Stop() call will not occur until after this one has returned via
  // the callback.
  if (zx_status_t status = ethernet_.Start(this, &ethernet_ifc_protocol_ops_); status != ZX_OK) {
    zxlogf(WARNING, "failed to start device: %s", zx_status_get_string(status));
    callback(cookie, status);
    return;
  }
  {
    std::lock_guard rx_lock(rx_lock_);
    std::lock_guard tx_lock(tx_lock_);
    rx_started_ = true;
    tx_started_ = true;
  }
  callback(cookie, ZX_OK);
}

void NetdeviceMigration::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                               void* cookie) __TA_EXCLUDES(rx_lock_, tx_lock_) {
  ethernet_.Stop();
  {
    std::lock_guard rx_lock(rx_lock_);
    std::lock_guard tx_lock(tx_lock_);
    rx_started_ = false;
    tx_started_ = false;
  }
  callback(cookie);
}

void NetdeviceMigration::NetworkDeviceImplGetInfo(device_info_t* out_info) { *out_info = info_; }

void NetdeviceMigration::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list,
                                                  size_t buffers_count)
    __TA_EXCLUDES(tx_lock_, vmo_lock_) {
  constexpr uint32_t kQueueOpts = 0;
  std::optional<Netbuf> args[kFifoDepth];
  auto args_iter = std::begin(args);
  {
    network::SharedAutoLock vmo_lock(&vmo_lock_);
    std::lock_guard tx_lock(tx_lock_);
    cpp20::span buffers(buffers_list, buffers_count);
    if (!tx_started_) {
      zxlogf(ERROR, "tx buffers queued before start call");
      tx_result_t results[buffers.size()];
      for (size_t i = 0; i < buffers.size(); ++i) {
        results[i] = {
            .id = buffers[i].id,
            .status = ZX_ERR_UNAVAILABLE,
        };
      }
      netdevice_.CompleteTx(results, buffers.size());
      return;
    }
    for (const tx_buffer_t& buffer : buffers) {
      if (buffer.data_count > info_.max_buffer_parts) {
        zxlogf(ERROR, "tx buffer queued with parts count %ld > max buffer parts %du",
               buffer.data_count, info_.max_buffer_parts);
        DdkAsyncRemove();
        return;
      }
      if (buffer.data_list->length > info_.max_buffer_length) {
        zxlogf(ERROR, "tx buffer queued with length %ld > max buffer length %du",
               buffer.data_list->length, info_.max_buffer_length);
        DdkAsyncRemove();
        return;
      }
      auto* vmo = vmo_store_.GetVmo(buffer.data_list->vmo);
      if (vmo == nullptr) {
        zxlogf(ERROR, "tx buffer %du queued with unknown vmo id %du", buffer.id,
               buffer.data_list->vmo);
        DdkAsyncRemove();
        return;
      }
      zx_paddr_t phys_addr = 0;
      if (eth_bti_.is_valid()) {
        fzl::PinnedVmo::Region region;
        size_t regions_needed = 0;
        if (zx_status_t status = vmo->GetPinnedRegions(
                buffer.data_list->offset, buffer.data_list->length, &region, 1, &regions_needed);
            status != ZX_OK) {
          zxlogf(ERROR, "failed to get pinned regions of vmo: %s", zx_status_get_string(status));
          tx_result_t result = {
              .id = buffer.id,
              .status = ZX_ERR_INTERNAL,
          };
          netdevice_.CompleteTx(&result, 1);
          continue;
        }
        phys_addr = region.phys_addr;
      }
      cpp20::span vmo_view(vmo->data());
      vmo_view = vmo_view.subspan(buffer.data_list->offset, buffer.data_list->length);
      std::optional netbuf = netbuf_pool_.pop();
      if (!netbuf.has_value()) {
        zxlogf(ERROR, "netbuf pool exhausted");
        DdkAsyncRemove();
        return;
      }
      *(netbuf->operation()) = {
          .data_buffer = vmo_view.data(),
          .data_size = vmo_view.size(),
          .phys = phys_addr,
      };
      *(netbuf->private_storage()) = buffer.id;
      *args_iter++ = std::move(netbuf);
    }
  }
  for (auto arg = std::begin(args); arg != args_iter; ++arg) {
    ethernet_.QueueTx(
        kQueueOpts, arg->value().take(),
        [](void* ctx, zx_status_t status, ethernet_netbuf_t* netbuf) {
          // The error semantics of fuchsia.hardware.ethernet/EthernetImpl.QueueTx are unspecified
          // other than `ZX_OK` indicating success. However, ethernet driver usages of
          // `ZX_ERR_NO_RESOURCES` and `ZX_ERR_UNAVAILABLE` map to the meanings specified by
          // fuchsia.hardware.network.device/TxResult. Accordingly, use `ZX_ERR_INTERNAL` for any
          // other Ethernet error.
          switch (status) {
            case ZX_OK:
            case ZX_ERR_NO_RESOURCES:
            case ZX_ERR_UNAVAILABLE:
              break;
            default:
              status = ZX_ERR_INTERNAL;
          }
          tx_result_t result = {
              .status = status,
          };
          auto* netdev = static_cast<NetdeviceMigration*>(ctx);
          Netbuf op(netbuf, netdev->netbuf_size_);
          result.id = *(op.private_storage());
          // Return the buffers to the netbuf_pool before signalling that the transaction is
          // complete. This ensures that if the netbuf_pool was empty, we can handle requests
          // that arrive immediately after.
          {
            std::lock_guard tx_lock(netdev->tx_lock_);
            netdev->netbuf_pool_.push(std::move(op));
          }
          netdev->netdevice_.CompleteTx(&result, 1);
        },
        this);
  }
}

void NetdeviceMigration::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                                       size_t buffers_count)
    __TA_EXCLUDES(rx_lock_) {
  std::lock_guard rx_lock(rx_lock_);
  if (size_t total_rx_buffers = rx_spaces_.size() + buffers_count;
      total_rx_buffers > info_.rx_depth) {
    // Client has violated API contract: "The total number of outstanding rx buffers given to a
    // device will never exceed the reported [`DeviceInfo.rx_depth`] value."
    zxlogf(ERROR, "total received rx buffers %ld > rx_depth %d", total_rx_buffers, info_.rx_depth);
    DdkAsyncRemove();
    return;
  }
  cpp20::span buffers(buffers_list, buffers_count);
  if (!rx_started_) {
    zxlogf(ERROR, "rx buffers queued before start call");
    for (const rx_space_buffer_t& space : buffers) {
      rx_buffer_part_t part = {
          .id = space.id,
          .length = 0,
      };
      rx_buffer_t buf = {
          .data_list = &part,
          .data_count = 1,
      };
      netdevice_.CompleteRx(&buf, 1);
    }
    return;
  }
  for (const rx_space_buffer_t& space : buffers) {
    if (space.region.length < info_.min_rx_buffer_length ||
        space.region.length > info_.max_buffer_length) {
      zxlogf(ERROR, "rx buffer queued with length %ld, outside valid range [%du, %du]",
             space.region.length, info_.min_rx_buffer_length, info_.max_buffer_length);
      DdkAsyncRemove();
      return;
    }
    rx_spaces_.push(space);
  }
}

void NetdeviceMigration::NetworkDeviceImplPrepareVmo(
    uint8_t id, zx::vmo vmo, network_device_impl_prepare_vmo_callback callback, void* cookie)
    __TA_EXCLUDES(vmo_lock_) {
  fbl::AutoLock vmo_lock(&vmo_lock_);
  zx_status_t status = vmo_store_.RegisterWithKey(id, std::move(vmo));
  callback(cookie, status);
}

void NetdeviceMigration::NetworkDeviceImplReleaseVmo(uint8_t id) __TA_EXCLUDES(vmo_lock_) {
  fbl::AutoLock vmo_lock(&vmo_lock_);
  if (zx::result<zx::vmo> status = vmo_store_.Unregister(id); status.status_value() != ZX_OK) {
    // A failure here may be the result of a failed call to register the vmo, in which case the
    // driver is queued for removal from device manager. Accordingly, we must not panic lest we
    // disrupt the orderly shutdown of the driver: a log statement is the best we can do.
    zxlogf(ERROR, "failed to release vmo id = %d: %s", id, status.status_string());
  }
}

void NetdeviceMigration::NetworkDeviceImplSetSnoop(bool snoop) {}

void NetdeviceMigration::NetworkPortGetInfo(port_info_t* out_info) { *out_info = port_info_; }

void NetdeviceMigration::NetworkPortGetStatus(port_status_t* out_status)
    __TA_EXCLUDES(status_lock_) {
  std::lock_guard lock(status_lock_);
  *out_status = {
      .mtu = mtu_,
      .flags = static_cast<uint32_t>(port_status_flags_),
  };
}

void NetdeviceMigration::NetworkPortSetActive(bool active) {}

void NetdeviceMigration::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  *out_mac_ifc = mac_addr_protocol_t{
      .ops = &mac_addr_protocol_ops_,
      .ctx = this,
  };
}

void NetdeviceMigration::NetworkPortRemoved() {
  zxlogf(INFO, "removed event for port %d", kPortId);
}

void NetdeviceMigration::MacAddrGetAddress(uint8_t out_mac[MAC_SIZE]) {
  static_assert(sizeof(mac_) == MAC_SIZE);
  std::copy(mac_.begin(), mac_.end(), out_mac);
}

void NetdeviceMigration::MacAddrGetFeatures(features_t* out_features) {
  *out_features = {
      .multicast_filter_count = kMulticastFilterMax,
      .supported_modes = kSupportedMacFilteringModes,
  };
}

void NetdeviceMigration::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                                        size_t multicast_macs_count) {
  if (multicast_macs_count > kMulticastFilterMax) {
    zxlogf(ERROR, "multicast macs count exceeds maximum: %zu > %du", multicast_macs_count,
           kMulticastFilterMax);
    DdkAsyncRemove();
    return;
  }
  switch (mode) {
    case MODE_MULTICAST_FILTER:
      SetMacParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, nullptr, 0);
      SetMacParam(ETHERNET_SETPARAM_PROMISC, 0, nullptr, 0);
      SetMacParam(ETHERNET_SETPARAM_MULTICAST_FILTER, static_cast<int32_t>(multicast_macs_count),
                  multicast_macs_list, multicast_macs_count * MAC_SIZE);
      break;
    case MODE_MULTICAST_PROMISCUOUS:
      SetMacParam(ETHERNET_SETPARAM_PROMISC, 0, nullptr, 0);
      SetMacParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
      break;
    case MODE_PROMISCUOUS:
      SetMacParam(ETHERNET_SETPARAM_PROMISC, 1, nullptr, 0);
      break;
    default:
      zxlogf(ERROR, "mac addr filtering mode set with unsupported mode %du", mode);
      DdkAsyncRemove();
      return;
  }
}

void NetdeviceMigration::SetMacParam(uint32_t param, int32_t value, const uint8_t* data_buffer,
                                     size_t data_size) const {
  if (zx_status_t status = ethernet_.SetParam(param, value, data_buffer, data_size);
      status != ZX_OK) {
    zxlogf(WARNING, "failed to set ethernet parameter %du to value %d: %s", param, value,
           zx_status_get_string(status));
  }
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
