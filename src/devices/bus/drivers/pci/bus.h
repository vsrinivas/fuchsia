// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_

#include <fuchsia/hardware/pci/llcpp/fidl.h>
#include <lib/zx/msi.h>
#include <zircon/compiler.h>

#include <list>
#include <memory>

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/pciroot.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/vector.h>

#include "bridge.h"
#include "bus_device_interface.h"
#include "config.h"
#include "device.h"
#include "root.h"

namespace pci {

// An entry corresponding to a place in the topology to scan. Use to allow for
// DFS traversal of the bus topology while keeping track of nodes upstream.
struct BusScanEntry {
  pci_bdf_t bdf;
  UpstreamNode* upstream;
};

using DeviceTree =
    fbl::WAVLTree<pci_bdf_t, fbl::RefPtr<pci::Device>, pci::Device::KeyTraitsSortByBdf>;
namespace PciFidl = llcpp::fuchsia::hardware::pci;

class Bus;
using PciBusType = ddk::Device<Bus, ddk::Messageable>;
class Bus : public PciBusType, public PciFidl::Bus::Interface, public BusDeviceInterface {
 public:
  static zx_status_t Create(zx_device_t* parent);
  void DdkRelease();

  // Accessors for the device list, used by BusDeviceInterface
  void LinkDevice(fbl::RefPtr<pci::Device> device) __TA_EXCLUDES(devices_lock_) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    devices_.insert(device);
  }

  void UnlinkDevice(pci::Device* device) __TA_EXCLUDES(devices_lock_) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    devices_.erase(*device);
  }

  zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) __TA_EXCLUDES(devices_lock_) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    return pciroot().AllocateMsi(count, false, msi);
  }

  zx_status_t GetBti(const pci::Device* device, uint32_t index, zx::bti* bti)
      __TA_EXCLUDES(devices_lock_) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    return pciroot().GetBti(device->packed_addr(), index, bti);
  }

  zx_status_t ConnectSysmem(zx::channel channel) __TA_EXCLUDES(devices_lock_) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    return pciroot().ConnectSysmem(std::move(channel));
  }

  // All methods related to the fuchsia.hardware.pci service.
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  virtual void GetDevices(GetDevicesCompleter::Sync& completer) final;
  virtual void GetHostBridgeInfo(GetHostBridgeInfoCompleter::Sync& completer) final;

 private:
  // Our constructor exists to fulfill the mixin constructors
  Bus(zx_device_t* parent, const pci_platform_info_t info, const pciroot_protocol_t* proto)
      : PciBusType(parent),  // fulfills the DDK mixins
        info_(info),
        pciroot_(proto) {}

  // Utility methods for the bus driver
  zx_status_t Initialize(void) __TA_EXCLUDES(devices_lock_);
  // Map an ecam VMO for Bus and Config use.
  zx_status_t MapEcam(void);
  // Scan all buses downstream from the root within the start and end
  // bus values given to the Bus driver through Pciroot.
  zx_status_t ScanDownstream(void);
  ddk::PcirootProtocolClient& pciroot(void) { return pciroot_; }
  // Scan a specific bus
  void ScanBus(BusScanEntry entry, std::list<BusScanEntry>* scan_list);
  // Creates a Config object for accessing the config space of the device at |bdf|.
  zx_status_t MakeConfig(pci_bdf_t bdf, std::unique_ptr<Config>* config);

  // members
  const pci_platform_info_t info_;
  ddk::PcirootProtocolClient pciroot_;
  std::optional<ddk::MmioBuffer> ecam_;
  std::unique_ptr<PciRoot> root_;
  fbl::Mutex devices_lock_;

  // All devices downstream of this bus are held here. Devices are keyed by
  // BDF so they will not experience any collisions.
  pci::DeviceTree devices_ __TA_GUARDED(devices_lock_);
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_
