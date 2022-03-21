// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_

#include <fidl/fuchsia.hardware.pci/cpp/wire.h>
#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/mmio/mmio.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/msi.h>
#include <lib/zx/thread.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/port.h>

#include <list>
#include <memory>
#include <thread>
#include <unordered_map>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/vector.h>

#include "src/devices/bus/drivers/pci/bridge.h"
#include "src/devices/bus/drivers/pci/bus_device_interface.h"
#include "src/devices/bus/drivers/pci/config.h"
#include "src/devices/bus/drivers/pci/device.h"
#include "src/devices/bus/drivers/pci/root.h"

namespace pci {
// The length of time to count interrupts before rolling over.
constexpr const zx_time_t kLegacyNoAckPeriod = ZX_SEC(1);
// The max number of interrupts that can be seen before disabling the device
// function's interrupt generation when in PCI_IRQ_MODE_LEGACY_NOACK.
constexpr const uint64_t kMaxIrqsPerNoAckPeriod = 512;

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
namespace PciFidl = fuchsia_hardware_pci;

// A tree of all pci Device objects in the bus topology.
using DeviceTree =
    fbl::WAVLTree<pci_bdf_t, fbl::RefPtr<pci::Device>, pci::Device::KeyTraitsSortByBdf>;

class Bus;
using PciBusType = ddk::Device<Bus, ddk::Messageable<PciFidl::Bus>::Mixin>;
class Bus : public PciBusType,
            public ddk::EmptyProtocol<ZX_PROTOCOL_PCI>,
            public BusDeviceInterface {
 public:
  static zx_status_t Create(zx_device_t* parent);
  Bus(zx_device_t* parent, const pciroot_protocol_t* pciroot, const pci_platform_info_t info,
      std::optional<fdf::MmioBuffer> ecam)
      : PciBusType(parent),  // fulfills the DDK mixins
        pciroot_(pciroot),
        info_(info),
        ecam_(std::move(ecam)) {}
  ~Bus() override;
  // Map an ecam VMO for Bus and Config use.
  static zx::status<fdf::MmioBuffer> MapEcam(zx::vmo ecam_vmo);

  zx_status_t Initialize() __TA_EXCLUDES(devices_lock_);
  // Bus Device Interface implementation
  zx_status_t LinkDevice(fbl::RefPtr<pci::Device> device) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t UnlinkDevice(pci::Device* device) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) __TA_EXCLUDES(devices_lock_) final;
  zx_status_t GetBti(const pci::Device* device, uint32_t index, zx::bti* bti)
      __TA_EXCLUDES(devices_lock_) final;
  zx_status_t AddToSharedIrqList(pci::Device* device, uint32_t vector)
      __TA_EXCLUDES(devices_lock_) final;
  zx_status_t RemoveFromSharedIrqList(pci::Device* device, uint32_t vector)
      __TA_EXCLUDES(devices_lock_) final;

  // All methods related to the fuchsia.hardware.pci service and the DDK.
  void DdkRelease() { delete this; }
  void GetDevices(GetDevicesRequestView request, GetDevicesCompleter::Sync& completer) final;
  void GetHostBridgeInfo(GetHostBridgeInfoRequestView request,
                         GetHostBridgeInfoCompleter::Sync& completer) final;

  zx::vmo GetInspectVmo() { return inspector_.DuplicateVmo(); }

 protected:
  // These are used by the derived TestBus class.
  fbl::Mutex* devices_lock() __TA_RETURN_CAPABILITY(devices_lock_) { return &devices_lock_; }
  pci::DeviceTree& devices() { return devices_; }
  SharedIrqMap& shared_irqs() { return shared_irqs_; }
  LegacyIrqs& legacy_irqs() { return legacy_irqs_; }

 private:
  // Map an ecam VMO for Bus and Config use.
  zx_status_t MapEcam();
  // Creates a Config object for accessing the config space of the device at |bdf|.
  zx_status_t MakeConfig(pci_bdf_t bdf, std::unique_ptr<Config>* config);
  // Scan all buses downstream from the root within the start and end
  // bus values given to the Bus driver through Pciroot.
  zx_status_t ScanDownstream();
  ddk::PcirootProtocolClient& pciroot() { return pciroot_; }
  // Scan a specific bus
  void ScanBus(BusScanEntry entry, std::list<BusScanEntry>* scan_list);
  // Returns true if a given BDF is present in the list of devices provided by
  // the platform to us that use the ACPI fragment.
  bool DeviceHasAcpi(pci_bdf_t bdf);

  // Creates interrupts corresponding to legacy IRQ vectors and configures devices accordingly.
  zx_status_t ConfigureLegacyIrqs() __TA_EXCLUDES(devices_lock_);
  // Creates and binds interrupts to the irq port and sets up Shared IRQ handler lists.
  zx_status_t SetUpLegacyIrqHandlers() __TA_REQUIRES(devices_lock_);
  static void LegacyIrqWorker(const zx::port& port, fbl::Mutex* lock, SharedIrqMap* shared_irq_map);
  // Creates and starts the legacy IRQ worker thread.
  void StartIrqWorker();
  // Queues a packet informing the IRQ worker that it should exit.
  zx_status_t StopIrqWorker();

  // members
  ddk::PcirootProtocolClient pciroot_;
  const pci_platform_info_t info_;
  std::optional<fdf::MmioBuffer> ecam_;
  cpp20::span<const pci_legacy_irq> irqs_{};
  cpp20::span<const pci_bdf_t> acpi_devices_{};
  cpp20::span<const pci_irq_routing_entry_t> irq_routing_entries_{};

  // All devices hang off of this Bus's root port.
  std::unique_ptr<PciRoot> root_;
  fbl::Mutex devices_lock_;
  // A port all legacy IRQs are bound to.
  zx::port legacy_irq_port_;
  std::thread irq_thread_;

  // All devices downstream of this bus are held here. Devices are keyed by
  // BDF so they will not experience any collisions.
  pci::DeviceTree devices_ __TA_GUARDED(devices_lock_);
  // A map of lists of devices corresponding to a give vector, keyed by vector.
  LegacyIrqs legacy_irqs_;
  SharedIrqMap shared_irqs_ __TA_GUARDED(devices_lock_);

  // Diagnostics.
  inspect::Inspector inspector_;
};

zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent);

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_BUS_H_
