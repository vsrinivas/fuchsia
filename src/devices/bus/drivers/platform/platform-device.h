// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_DEVICE_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_DEVICE_H_

#include <lib/zx/bti.h>
#include <lib/zx/channel.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/vector.h>

#include "device-resources.h"
#include "proxy-protocol.h"

// This class, along with PlatformProxyDevice, represent a platform device.
// Platform devices run in separate devhosts than the platform bus driver.
// PlatformDevice exists in the platform bus devhost and PlatformProxyDevice
// exists in the platform device's devhost. PlatformProxyDevice proxys
// requests from the platform device via a channel, which are then
// handled by PlatformDevice::DdkRxrpc and then handled by relevant Rpc* methods.
//
// Resource handles passed to the proxy to allow it to access MMIOs and interrupts.
// This ensures if the proxy driver dies we will release their address space resources
// back to the kernel if necessary.

namespace platform_bus {

class PlatformBus;

class PlatformDevice;
using PlatformDeviceType = ddk::Device<PlatformDevice, ddk::GetProtocolable, ddk::Rxrpcable>;

// This class represents a platform device attached to the platform bus.
// Instances of this class are created by PlatformBus at boot time when the board driver
// calls the platform bus protocol method pbus_device_add().

class PlatformDevice : public PlatformDeviceType,
                       public ddk::PDevProtocol<PlatformDevice, ddk::base_protocol> {
 public:
  enum Type {
    // This platform device is started in a new devhost.
    // The PDEV protocol is proxied to the PlatformProxy driver in the new devhost.
    Isolated,
    // This platform device is run in the same process as platform bus and provides
    // its protocol to the platform bus.
    Protocol,
    // This platform device is a component for a composite device.
    // The PDEV protocol is proxied by the devmgr "component" driver.
    Component,
  };

  // Creates a new PlatformDevice instance.
  // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
  static zx_status_t Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                            Type type, std::unique_ptr<platform_bus::PlatformDevice>* out);

  inline uint32_t vid() const { return vid_; }
  inline uint32_t pid() const { return pid_; }
  inline uint32_t did() const { return did_; }

  // Device protocol implementation.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  zx_status_t DdkRxrpc(zx_handle_t channel);
  void DdkRelease();

  // Platform device protocol implementation, for devices that run in-process.
  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_handle);
  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource);
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
  zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args, zx_device_t** device);

  // Starts the underlying devmgr device.
  zx_status_t Start();

 private:
  // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
  explicit PlatformDevice(zx_device_t* parent, PlatformBus* bus, Type type, const pbus_dev_t* pdev);
  zx_status_t Init(const pbus_dev_t* pdev);

  // Handlers for RPCs from PlatformProxy.
  zx_status_t RpcGetMmio(uint32_t index, zx_paddr_t* out_paddr, size_t* out_length,
                         zx_handle_t* out_handle, uint32_t* out_handle_count);
  zx_status_t RpcGetInterrupt(uint32_t index, uint32_t* out_irq, uint32_t* out_mode,
                              zx_handle_t* out_handle, uint32_t* out_handle_count);
  zx_status_t RpcGetBti(uint32_t index, zx_handle_t* out_handle, uint32_t* out_handle_count);
  zx_status_t RpcGetSmc(uint32_t index, zx_handle_t* out_handle, uint32_t* out_handle_count);
  zx_status_t RpcGetDeviceInfo(pdev_device_info_t* out_info);
  zx_status_t RpcGetMetadata(uint32_t index, uint32_t* out_type, uint8_t* buf, uint32_t buf_size,
                             uint32_t* actual);

  PlatformBus* bus_;
  char name_[ZX_DEVICE_NAME_MAX + 1];
  Type type_;
  const uint32_t vid_;
  const uint32_t pid_;
  const uint32_t did_;

  // Platform bus resources for this device
  DeviceResources resources_;

  // Restricted subset of the platform bus protocol.
  // We do not allow protocol devices call pbus_device_add() or pbus_protocol_device_add()
  pbus_protocol_ops_t pbus_ops_;
  void* pbus_ctx_;
};

}  // namespace platform_bus

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_DEVICE_H_
