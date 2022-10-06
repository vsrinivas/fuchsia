// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_DEVICE_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_DEVICE_H_

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/natural_types.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/driver2/outgoing_directory.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>

#include <ddktl/device.h>
#include <fbl/vector.h>

#include "proxy-protocol.h"
#include "src/devices/bus/drivers/platform/platform-interrupt.h"

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

// Restricted version of the platform bus protocol that does not allow devices to be added.
class RestrictPlatformBus : public fdf::WireServer<fuchsia_hardware_platform_bus::PlatformBus> {
 public:
  RestrictPlatformBus(PlatformBus* upstream) : upstream_(upstream) {}
  // fuchsia.hardware.platform.bus.PlatformBus implementation.
  void NodeAdd(NodeAddRequestView request, fdf::Arena& arena,
               NodeAddCompleter::Sync& completer) override;
  void ProtocolNodeAdd(ProtocolNodeAddRequestView request, fdf::Arena& arena,
                       ProtocolNodeAddCompleter::Sync& completer) override;
  void RegisterProtocol(RegisterProtocolRequestView request, fdf::Arena& arena,
                        RegisterProtocolCompleter::Sync& completer) override;

  void GetBoardInfo(fdf::Arena& arena, GetBoardInfoCompleter::Sync& completer) override;
  void SetBoardInfo(SetBoardInfoRequestView request, fdf::Arena& arena,
                    SetBoardInfoCompleter::Sync& completer) override;
  void SetBootloaderInfo(SetBootloaderInfoRequestView request, fdf::Arena& arena,
                         SetBootloaderInfoCompleter::Sync& completer) override;

  void RegisterSysSuspendCallback(RegisterSysSuspendCallbackRequestView request, fdf::Arena& arena,
                                  RegisterSysSuspendCallbackCompleter::Sync& completer) override;
  void AddComposite(AddCompositeRequestView request, fdf::Arena& arena,
                    AddCompositeCompleter::Sync& completer) override;

  void AddCompositeImplicitPbusFragment(
      AddCompositeImplicitPbusFragmentRequestView request, fdf::Arena& arena,
      AddCompositeImplicitPbusFragmentCompleter::Sync& completer) override;

 private:
  PlatformBus* upstream_;
};

class PlatformDevice;
using PlatformDeviceType =
    ddk::Device<PlatformDevice, ddk::GetProtocolable, ddk::Rxrpcable, ddk::Initializable>;

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
    // This platform device is a fragment for a composite device.
    // The PDEV protocol is proxied by the devmgr "fragment" driver.
    Fragment,
  };

  // Creates a new PlatformDevice instance.
  // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
  static zx_status_t Create(fuchsia_hardware_platform_bus::Node node, zx_device_t* parent,
                            PlatformBus* bus, Type type,
                            std::unique_ptr<platform_bus::PlatformDevice>* out);

  inline uint32_t vid() const { return vid_; }
  inline uint32_t pid() const { return pid_; }
  inline uint32_t did() const { return did_; }
  inline uint32_t instance_id() const { return instance_id_; }

  // Device protocol implementation.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  zx_status_t DdkRxrpc(zx_handle_t channel);
  void DdkInit(ddk::InitTxn txn);
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
  explicit PlatformDevice(zx_device_t* parent, PlatformBus* bus, Type type,
                          fuchsia_hardware_platform_bus::Node node);
  zx_status_t Init();

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

  zx_status_t CreateInterruptFragments();

  PlatformBus* bus_;
  char name_[ZX_DEVICE_NAME_MAX + 1];
  Type type_;
  const uint32_t vid_;
  const uint32_t pid_;
  const uint32_t did_;
  const uint32_t instance_id_;

  fuchsia_hardware_platform_bus::Node node_;
  std::unique_ptr<RestrictPlatformBus> restricted_;
  driver::OutgoingDirectory outgoing_;
};

}  // namespace platform_bus

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_DEVICE_H_
