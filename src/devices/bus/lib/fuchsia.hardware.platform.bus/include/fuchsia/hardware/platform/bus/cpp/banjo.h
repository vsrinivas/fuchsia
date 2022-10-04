// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_H_
#define SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_H_

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddktl/device-internal.h>

#include "banjo-internal.h"
#include "lib/fidl/cpp/wire/arena.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

// DDK bus-protocol support
//
// :: Proxies ::
//
// ddk::PBusProtocolClient is a simple wrapper around
// pbus_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::PBusProtocol is a mixin class that simplifies writing DDK drivers
// that implement the pbus protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PBUS device.
// class PBusDevice;
// using PBusDeviceType = ddk::Device<PBusDevice, /* ddk mixins */>;
//
// class PBusDevice : public PBusDeviceType,
//                      public ddk::PBusProtocol<PBusDevice> {
//   public:
//     PBusDevice(zx_device_t* parent)
//         : PBusDeviceType(parent) {}
//
//     zx_status_t PBusDeviceAdd(const pbus_dev_t* dev);
//
//     zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev);
//
//     zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol_buffer, size_t
//     protocol_size);
//
//     zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info);
//
//     zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info);
//
//     zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info);
//
//     zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cb);
//
//     zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev, uint64_t fragments, uint64_t
//     fragments_count, const char* primary_fragment);
//
//     zx_status_t PBusAddComposite(const pbus_dev_t* dev, uint64_t fragments, uint64_t
//     fragment_count, const char* primary_fragment);
//
//     ...
// };

namespace ddk {

class PBusProtocolClient {
 public:
  PBusProtocolClient() {}

  PBusProtocolClient(zx_device_t* parent) {
    auto endpoints = fdf::CreateEndpoints<fuchsia_hardware_platform_bus::PlatformBus>();
    if (endpoints.is_error()) {
      zxlogf(ERROR, "Creation of endpoints failed: %s", endpoints.status_string());
      return;
    }

    zx_status_t status = device_connect_runtime_protocol(
        parent, fuchsia_hardware_platform_bus::Service::PlatformBus::ServiceName,
        fuchsia_hardware_platform_bus::Service::PlatformBus::Name,
        endpoints->server.TakeHandle().release());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to connect to platform bus: %s", zx_status_get_string(status));
      return;
    }

    client_.Bind(std::move(endpoints->client));
  }

  // Create a PBusProtocolClient from the given parent device + "fragment".
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, PBusProtocolClient* result) {
    *result = PBusProtocolClient(parent);
    if (result->is_valid()) {
      return ZX_OK;
    } else {
      return ZX_ERR_INTERNAL;
    }
  }

  bool is_valid() const { return client_.is_valid(); }
  void clear() { client_ = {}; }

  // Adds a new platform device to the bus, using configuration provided by |dev|.
  // Platform devices are created in their own separate devhosts.
  zx_status_t DeviceAdd(const pbus_dev_t* dev) const {
    fidl::Arena<> fidl_arena;

    auto result = client_.buffer(fdf::Arena('PBAD'))->NodeAdd(DevToNode(dev, fidl_arena));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: NodeAdd request failed: %s", __func__, result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: NodeAdd failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

  // Adds a device for binding a protocol implementation driver.
  // These devices are added in the same devhost as the platform bus.
  // After the driver binds to the device it calls `pbus_register_protocol()`
  // to register its protocol with the platform bus.
  // `pbus_protocol_device_add()` blocks until the protocol implementation driver
  // registers its protocol (or times out).
  zx_status_t ProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) const {
    fidl::Arena<> fidl_arena;
    auto result =
        client_.buffer(fdf::Arena('PBPD'))->ProtocolNodeAdd(proto_id, DevToNode(dev, fidl_arena));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: ProtocolNodeAdd request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: ProtocolNodeAdd failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

  // Called by protocol implementation drivers to register their protocol
  // with the platform bus.
  zx_status_t RegisterProtocol(uint32_t proto_id, const uint8_t* protocol_buffer,
                               size_t protocol_size) const {
    auto result =
        client_.buffer(fdf::Arena('PBRP'))
            ->RegisterProtocol(proto_id, fidl::VectorView<uint8_t>::FromExternal(
                                             const_cast<uint8_t*>(protocol_buffer), protocol_size));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: RegisterProtocol request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: RegisterProtocol failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

  // Board drivers may use this to get information about the board, and to
  // differentiate between multiple boards that they support.
  zx_status_t GetBoardInfo(pdev_board_info_t* out_info) const {
    auto result = client_.buffer(fdf::Arena('PDGB'))->GetBoardInfo();
    if (!result.ok()) {
      zxlogf(ERROR, "%s: GetBoardInfo request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: GetBoardInfo failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    auto& info = result->value()->info;
    out_info->vid = info.vid;
    out_info->pid = info.pid;
    out_info->board_revision = info.board_revision;
    memset(out_info->board_name, 0, sizeof(out_info->board_name));
    memcpy(out_info->board_name, info.board_name.data(),
           std::min(info.board_name.size(), sizeof(out_info->board_name) - 1));

    return ZX_OK;
  }

  // Board drivers may use this to set information about the board
  // (like the board revision number).
  // Platform device drivers can access this via `pdev_get_board_info()`.
  zx_status_t SetBoardInfo(const pbus_board_info_t* info) const {
    fidl::Arena<> fidl_arena;
    auto result =
        client_.buffer(fdf::Arena('PBSB'))
            ->SetBoardInfo(fuchsia_hardware_platform_bus::wire::BoardInfo::Builder(fidl_arena)
                               .board_name(fidl::StringView::FromExternal(info->board_name))
                               .board_revision(info->board_revision)
                               .Build());
    if (!result.ok()) {
      zxlogf(ERROR, "%s: SetBoardInfo request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: SetBoardInfo failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

  // Board drivers may use this to set information about the bootloader.
  zx_status_t SetBootloaderInfo(const pbus_bootloader_info_t* info) const {
    fidl::Arena<> fidl_arena;
    auto result = client_.buffer(fdf::Arena('PBBI'))
                      ->SetBootloaderInfo(
                          fuchsia_hardware_platform_bus::wire::BootloaderInfo::Builder(fidl_arena)
                              .vendor(fidl::StringView::FromExternal(info->vendor))
                              .Build());

    if (!result.ok()) {
      zxlogf(ERROR, "%s: SetBootloaderInfo request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: SetBootloaderInfo failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

  zx_status_t RegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cb) const {
    // No users outside of the x86 board driver.
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Deprecated, use AddComposite() instead.
  // Adds a composite platform device to the bus. The platform device specified by |dev|
  // is the zeroth fragment and the |fragments| array specifies fragments 1 through n.
  // The composite device is started in a the driver host of the
  // |primary_fragment| if it is specified, or a new driver host if it is is
  // NULL. It is not possible to set the primary fragment to "pdev" as that
  // would cause the driver to spawn in the platform bus's driver host.
  zx_status_t CompositeDeviceAdd(const pbus_dev_t* dev, uint64_t fragments,
                                 uint64_t fragments_count, const char* primary_fragment) const {
    if (!primary_fragment) {
      zxlogf(ERROR, "%s: primary_fragment cannot be null", __func__);
      return ZX_ERR_INVALID_ARGS;
    }

    fidl::Arena<> fidl_arena;
    auto result =
        client_.buffer(fdf::Arena('PBCD'))
            ->AddCompositeImplicitPbusFragment(
                DevToNode(dev, fidl_arena),
                platform_bus_composite::MakeFidlFragment(
                    fidl_arena, reinterpret_cast<device_fragment_t*>(fragments), fragments_count),
                fidl::StringView::FromExternal(primary_fragment));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

  // Adds a composite platform device to the bus.
  zx_status_t AddComposite(const pbus_dev_t* dev, uint64_t fragments, uint64_t fragment_count,
                           const char* primary_fragment) const {
    fidl::Arena<> fidl_arena;
    auto result =
        client_.buffer(fdf::Arena('PBAC'))
            ->AddComposite(
                DevToNode(dev, fidl_arena),
                platform_bus_composite::MakeFidlFragment(
                    fidl_arena, reinterpret_cast<device_fragment_t*>(fragments), fragment_count),
                fidl::StringView::FromExternal(primary_fragment));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
    return ZX_OK;
  }

 private:
  fuchsia_hardware_platform_bus::wire::Node DevToNode(const pbus_dev_t* dev,
                                                      fidl::AnyArena& arena) const {
    auto builder = fuchsia_hardware_platform_bus::wire::Node::Builder(arena);
    builder.name(fidl::StringView::FromExternal(dev->name));
    builder.vid(dev->vid);
    builder.pid(dev->pid);
    builder.did(dev->did);
    builder.instance_id(dev->instance_id);

    fidl::VectorView<fuchsia_hardware_platform_bus::wire::Mmio> mmios(arena, dev->mmio_count);
    for (size_t i = 0; i < dev->mmio_count; i++) {
      auto* mmio = &dev->mmio_list[i];
      mmios[i] = fuchsia_hardware_platform_bus::wire::Mmio::Builder(arena)
                     .base(mmio->base)
                     .length(mmio->length)
                     .Build();
    }
    builder.mmio(mmios);

    fidl::VectorView<fuchsia_hardware_platform_bus::wire::Irq> irqs(arena, dev->irq_count);
    for (size_t i = 0; i < dev->irq_count; i++) {
      auto* irq = &dev->irq_list[i];
      irqs[i] = fuchsia_hardware_platform_bus::wire::Irq::Builder(arena)
                    .irq(irq->irq)
                    .mode(irq->mode)
                    .Build();
    }
    builder.irq(irqs);

    fidl::VectorView<fuchsia_hardware_platform_bus::wire::Bti> btis(arena, dev->bti_count);
    for (size_t i = 0; i < dev->bti_count; i++) {
      auto* bti = &dev->bti_list[i];
      btis[i] = fuchsia_hardware_platform_bus::wire::Bti::Builder(arena)
                    .iommu_index(bti->iommu_index)
                    .bti_id(bti->bti_id)
                    .Build();
    }
    builder.bti(btis);

    fidl::VectorView<fuchsia_hardware_platform_bus::wire::Smc> smcs(arena, dev->smc_count);
    for (size_t i = 0; i < dev->smc_count; i++) {
      auto* smc = &dev->smc_list[i];
      smcs[i] = fuchsia_hardware_platform_bus::wire::Smc::Builder(arena)
                    .count(smc->count)
                    .service_call_num_base(smc->service_call_num_base)
                    .exclusive(smc->exclusive)
                    .Build();
    }
    builder.smc(smcs);

    fidl::VectorView<fuchsia_hardware_platform_bus::wire::Metadata> metadatas(arena,
                                                                              dev->metadata_count);
    for (size_t i = 0; i < dev->metadata_count; i++) {
      auto* metadata = &dev->metadata_list[i];
      metadatas[i] = fuchsia_hardware_platform_bus::wire::Metadata::Builder(arena)
                         .data(fidl::VectorView<uint8_t>::FromExternal(
                             const_cast<uint8_t*>(metadata->data_buffer), metadata->data_size))
                         .type(metadata->type)
                         .Build();
    }
    builder.metadata(metadatas);

    fidl::VectorView<fuchsia_hardware_platform_bus::wire::BootMetadata> boot_metadatas(
        arena, dev->boot_metadata_count);
    for (size_t i = 0; i < dev->boot_metadata_count; i++) {
      auto* boot_metadata = &dev->boot_metadata_list[i];
      boot_metadatas[i] = fuchsia_hardware_platform_bus::wire::BootMetadata::Builder(arena)
                              .zbi_type(boot_metadata->zbi_type)
                              .zbi_extra(boot_metadata->zbi_extra)
                              .Build();
    }
    builder.boot_metadata(boot_metadatas);

    return builder.Build();
  }

  fdf::WireSyncClient<fuchsia_hardware_platform_bus::PlatformBus> client_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_H_
