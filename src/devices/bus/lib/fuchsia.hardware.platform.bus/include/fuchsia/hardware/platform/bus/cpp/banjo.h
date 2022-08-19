// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.platform.bus banjo file

#ifndef SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_H_
#define SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_H_

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddktl/device-internal.h>

#include "banjo-internal.h"

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

template <typename D, typename Base = internal::base_mixin>
class PBusProtocol : public Base {
 public:
  PBusProtocol() {
    internal::CheckPBusProtocolSubclass<D>();
    pbus_protocol_ops_.device_add = PBusDeviceAdd;
    pbus_protocol_ops_.protocol_device_add = PBusProtocolDeviceAdd;
    pbus_protocol_ops_.register_protocol = PBusRegisterProtocol;
    pbus_protocol_ops_.get_board_info = PBusGetBoardInfo;
    pbus_protocol_ops_.set_board_info = PBusSetBoardInfo;
    pbus_protocol_ops_.set_bootloader_info = PBusSetBootloaderInfo;
    pbus_protocol_ops_.register_sys_suspend_callback = PBusRegisterSysSuspendCallback;
    pbus_protocol_ops_.composite_device_add = PBusCompositeDeviceAdd;
    pbus_protocol_ops_.add_composite = PBusAddComposite;

    if constexpr (internal::is_base_proto<Base>::value) {
      auto dev = static_cast<D*>(this);
      // Can only inherit from one base_protocol implementation.
      ZX_ASSERT(dev->ddk_proto_id_ == 0);
      dev->ddk_proto_id_ = ZX_PROTOCOL_PBUS;
      dev->ddk_proto_ops_ = &pbus_protocol_ops_;
    }
  }

 protected:
  pbus_protocol_ops_t pbus_protocol_ops_ = {};

 private:
  // Adds a new platform device to the bus, using configuration provided by |dev|.
  // Platform devices are created in their own separate devhosts.
  static zx_status_t PBusDeviceAdd(void* ctx, const pbus_dev_t* dev) {
    auto ret = static_cast<D*>(ctx)->PBusDeviceAdd(dev);
    return ret;
  }
  // Adds a device for binding a protocol implementation driver.
  // These devices are added in the same devhost as the platform bus.
  // After the driver binds to the device it calls `pbus_register_protocol()`
  // to register its protocol with the platform bus.
  // `pbus_protocol_device_add()` blocks until the protocol implementation driver
  // registers its protocol (or times out).
  static zx_status_t PBusProtocolDeviceAdd(void* ctx, uint32_t proto_id, const pbus_dev_t* dev) {
    auto ret = static_cast<D*>(ctx)->PBusProtocolDeviceAdd(proto_id, dev);
    return ret;
  }
  // Called by protocol implementation drivers to register their protocol
  // with the platform bus.
  static zx_status_t PBusRegisterProtocol(void* ctx, uint32_t proto_id,
                                          const uint8_t* protocol_buffer, size_t protocol_size) {
    auto ret = static_cast<D*>(ctx)->PBusRegisterProtocol(proto_id, protocol_buffer, protocol_size);
    return ret;
  }
  // Board drivers may use this to get information about the board, and to
  // differentiate between multiple boards that they support.
  static zx_status_t PBusGetBoardInfo(void* ctx, pdev_board_info_t* out_info) {
    auto ret = static_cast<D*>(ctx)->PBusGetBoardInfo(out_info);
    return ret;
  }
  // Board drivers may use this to set information about the board
  // (like the board revision number).
  // Platform device drivers can access this via `pdev_get_board_info()`.
  static zx_status_t PBusSetBoardInfo(void* ctx, const pbus_board_info_t* info) {
    auto ret = static_cast<D*>(ctx)->PBusSetBoardInfo(info);
    return ret;
  }
  // Board drivers may use this to set information about the bootloader.
  static zx_status_t PBusSetBootloaderInfo(void* ctx, const pbus_bootloader_info_t* info) {
    auto ret = static_cast<D*>(ctx)->PBusSetBootloaderInfo(info);
    return ret;
  }
  static zx_status_t PBusRegisterSysSuspendCallback(void* ctx,
                                                    const pbus_sys_suspend_t* suspend_cb) {
    auto ret = static_cast<D*>(ctx)->PBusRegisterSysSuspendCallback(suspend_cb);
    return ret;
  }
  // Deprecated, use AddComposite() instead.
  // Adds a composite platform device to the bus. The platform device specified by |dev|
  // is the zeroth fragment and the |fragments| array specifies fragments 1 through n.
  // The composite device is started in a the driver host of the
  // |primary_fragment| if it is specified, or a new driver host if it is is
  // NULL. It is not possible to set the primary fragment to "pdev" as that
  // would cause the driver to spawn in the platform bus's driver host.
  static zx_status_t PBusCompositeDeviceAdd(void* ctx, const pbus_dev_t* dev, uint64_t fragments,
                                            uint64_t fragments_count,
                                            const char* primary_fragment) {
    auto ret = static_cast<D*>(ctx)->PBusCompositeDeviceAdd(dev, fragments, fragments_count,
                                                            primary_fragment);
    return ret;
  }
  // Adds a composite platform device to the bus.
  static zx_status_t PBusAddComposite(void* ctx, const pbus_dev_t* dev, uint64_t fragments,
                                      uint64_t fragment_count, const char* primary_fragment) {
    auto ret =
        static_cast<D*>(ctx)->PBusAddComposite(dev, fragments, fragment_count, primary_fragment);
    return ret;
  }
};

class PBusProtocolClient {
 public:
  PBusProtocolClient() : ops_(nullptr), ctx_(nullptr) {}
  PBusProtocolClient(const pbus_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

  PBusProtocolClient(zx_device_t* parent) {
    pbus_protocol_t proto;
    if (device_get_protocol(parent, ZX_PROTOCOL_PBUS, &proto) == ZX_OK) {
      ops_ = proto.ops;
      ctx_ = proto.ctx;
    } else {
      ops_ = nullptr;
      ctx_ = nullptr;
    }
  }

  PBusProtocolClient(zx_device_t* parent, const char* fragment_name) {
    pbus_protocol_t proto;
    if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_PBUS, &proto) == ZX_OK) {
      ops_ = proto.ops;
      ctx_ = proto.ctx;
    } else {
      ops_ = nullptr;
      ctx_ = nullptr;
    }
  }

  // Create a PBusProtocolClient from the given parent device + "fragment".
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, PBusProtocolClient* result) {
    pbus_protocol_t proto;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &proto);
    if (status != ZX_OK) {
      return status;
    }
    *result = PBusProtocolClient(&proto);
    return ZX_OK;
  }

  // Create a PBusProtocolClient from the given parent device.
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                      PBusProtocolClient* result) {
    pbus_protocol_t proto;
    zx_status_t status =
        device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_PBUS, &proto);
    if (status != ZX_OK) {
      return status;
    }
    *result = PBusProtocolClient(&proto);
    return ZX_OK;
  }

  void GetProto(pbus_protocol_t* proto) const {
    proto->ctx = ctx_;
    proto->ops = ops_;
  }
  bool is_valid() const { return ops_ != nullptr; }
  void clear() {
    ctx_ = nullptr;
    ops_ = nullptr;
  }

  // Adds a new platform device to the bus, using configuration provided by |dev|.
  // Platform devices are created in their own separate devhosts.
  zx_status_t DeviceAdd(const pbus_dev_t* dev) const { return ops_->device_add(ctx_, dev); }

  // Adds a device for binding a protocol implementation driver.
  // These devices are added in the same devhost as the platform bus.
  // After the driver binds to the device it calls `pbus_register_protocol()`
  // to register its protocol with the platform bus.
  // `pbus_protocol_device_add()` blocks until the protocol implementation driver
  // registers its protocol (or times out).
  zx_status_t ProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) const {
    return ops_->protocol_device_add(ctx_, proto_id, dev);
  }

  // Called by protocol implementation drivers to register their protocol
  // with the platform bus.
  zx_status_t RegisterProtocol(uint32_t proto_id, const uint8_t* protocol_buffer,
                               size_t protocol_size) const {
    return ops_->register_protocol(ctx_, proto_id, protocol_buffer, protocol_size);
  }

  // Board drivers may use this to get information about the board, and to
  // differentiate between multiple boards that they support.
  zx_status_t GetBoardInfo(pdev_board_info_t* out_info) const {
    return ops_->get_board_info(ctx_, out_info);
  }

  // Board drivers may use this to set information about the board
  // (like the board revision number).
  // Platform device drivers can access this via `pdev_get_board_info()`.
  zx_status_t SetBoardInfo(const pbus_board_info_t* info) const {
    return ops_->set_board_info(ctx_, info);
  }

  // Board drivers may use this to set information about the bootloader.
  zx_status_t SetBootloaderInfo(const pbus_bootloader_info_t* info) const {
    return ops_->set_bootloader_info(ctx_, info);
  }

  zx_status_t RegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cb) const {
    return ops_->register_sys_suspend_callback(ctx_, suspend_cb);
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
    return ops_->composite_device_add(ctx_, dev, fragments, fragments_count, primary_fragment);
  }

  // Adds a composite platform device to the bus.
  zx_status_t AddComposite(const pbus_dev_t* dev, uint64_t fragments, uint64_t fragment_count,
                           const char* primary_fragment) const {
    return ops_->add_composite(ctx_, dev, fragments, fragment_count, primary_fragment);
  }

 private:
  pbus_protocol_ops_t* ops_;
  void* ctx_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_H_
