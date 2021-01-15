// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_

#include <fuchsia/hardware/pci/llcpp/fidl.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/msi.h>
#include <zircon/compiler.h>

#include <list>
#include <memory>
#include <unordered_map>

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/intrusive_double_list.h>
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

// A list of pci::Device which share the same legacy IRQ and are configured to use legacy irqs.
using SharedIrqList = fbl::TaggedDoublyLinkedList<pci::Device*, SharedIrqListTag>;
struct SharedVector {
  zx::interrupt interrupt;
  SharedIrqList list;
};

using LegacyIrqs = std::unordered_map<uint32_t, pci_legacy_irq>;
// A map of vector -> SharedVector for use in handling interrupts.
using SharedIrqMap = std::unordered_map<uint32_t, std::unique_ptr<SharedVector>>;
namespace PciFidl = llcpp::fuchsia::hardware::pci;

// A tree of all pci Device objects in the bus topology.
using DeviceTree =
    fbl::WAVLTree<pci_bdf_t, fbl::RefPtr<pci::Device>, pci::Device::KeyTraitsSortByBdf>;

class Bus;
using PciBusType = ddk::Device<Bus, ddk::Messageable>;
class Bus : public PciBusType, public PciFidl::Bus::Interface, public BusDeviceInterface {
 public:
  static zx_status_t Create(zx_device_t* parent);
  Bus(zx_device_t* parent, const pciroot_protocol_t* pciroot, const pci_platform_info_t info,
      std::optional<ddk::MmioBuffer> ecam)
      : PciBusType(parent),  // fulfills the DDK mixins
        pciroot_(pciroot),
        info_(info),
        ecam_(std::move(ecam)) {}
  ~Bus() override;
  zx_status_t Initialize() __TA_EXCLUDES(devices_lock_);
  // Map an ecam VMO for Bus and Config use.
  static zx::status<ddk::MmioBuffer> MapEcam(zx::vmo ecam_vmo);
  void DdkRelease();

  // Bus Device Interface implementation
  zx_status_t LinkDevice(fbl::RefPtr<pci::Device> device) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t UnlinkDevice(pci::Device* device) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t ConnectSysmem(zx::channel channel) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t GetBti(const pci::Device* device, uint32_t index, zx::bti* bti)
      __TA_EXCLUDES(devices_lock_) final;
  zx_status_t AddToSharedIrqList(pci::Device* device, uint32_t vector)
      __TA_EXCLUDES(devices_lock_) final;
  zx_status_t RemoveFromSharedIrqList(pci::Device* device, uint32_t vector)
      __TA_EXCLUDES(devices_lock_) final;

  // All methods related to the fuchsia.hardware.pci service.
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void GetDevices(GetDevicesCompleter::Sync& completer) final;
  void GetHostBridgeInfo(GetHostBridgeInfoCompleter::Sync& completer) final;

 protected:
  // These are used for derived test classes.
  fbl::Mutex* devices_lock() __TA_RETURN_CAPABILITY(devices_lock_) { return &devices_lock_; }
  pci::DeviceTree& devices() { return devices_; }
  SharedIrqMap& shared_irqs() { return shared_irqs_; }
  LegacyIrqs& legacy_irqs() { return legacy_irqs_; }

 private:
  // bus values given to the Bus driver through Pciroot.
  // Creates a Config object for accessing the config space of the device at |bdf|.
  zx_status_t MakeConfig(pci_bdf_t bdf, std::unique_ptr<Config>* config);
  // Scan all buses downstream from the root within the start and end
  zx_status_t ScanDownstream();
  ddk::PcirootProtocolClient& pciroot() { return pciroot_; }
  // Scan a specific bus
  void ScanBus(BusScanEntry entry, std::list<BusScanEntry>* scan_list);

  // All methods related to shared IRQ handling around legacy interrupts.
  // Creates interrupts corresponding to legacy IRQ vectors and configures devices accordingly.
  zx_status_t ConfigureLegacyIrqs();

  // members
  ddk::PcirootProtocolClient pciroot_;
  const pci_platform_info_t info_;
  std::optional<ddk::MmioBuffer> ecam_;
  // All devices hang off of this Bus's root port.
  std::unique_ptr<PciRoot> root_;
  fbl::Mutex devices_lock_;
  // A port all legacy IRQs are bound to.
  zx::port legacy_irq_port_;

  // All devices downstream of this bus are held here. Devices are keyed by
  // BDF so they will not experience any collisions.
  pci::DeviceTree devices_ __TA_GUARDED(devices_lock_);
  // A map of lists of devices corresponding to a give vector, keyed by vector.
  LegacyIrqs legacy_irqs_;
  SharedIrqMap shared_irqs_ __TA_GUARDED(devices_lock_);
};

zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent);

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_
