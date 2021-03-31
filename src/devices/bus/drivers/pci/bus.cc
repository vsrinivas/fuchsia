// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/pci/bus.h"

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/time.h>
#include <zircon/hw/pci.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>

#include <list>
#include <thread>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/span.h>
#include <fbl/vector.h>

#include "src/devices/bus/drivers/pci/bridge.h"
#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/config.h"
#include "src/devices/bus/drivers/pci/device.h"

namespace pci {

zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent) {
  pciroot_protocol_t pciroot = {};
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &pciroot);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to obtain pciroot protocol: %s", zx_status_get_string(status));
    return status;
  }

  pci_platform_info_t info = {};
  status = pciroot_get_pci_platform_info(&pciroot, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to obtain platform information: %s!", zx_status_get_string(status));
    return status;
  }

  // A PCI bus should have an ecam, but they are not mandatory per spec depending on the
  // platform tables offered to us.
  std::optional<ddk::MmioBuffer> ecam;
  if (info.ecam_vmo != ZX_HANDLE_INVALID) {
    if (auto result = pci::Bus::MapEcam(zx::vmo(info.ecam_vmo)); result.is_ok()) {
      ecam = std::move(result.value());
    } else {
      return result.status_value();
    }
  }

  auto bus = std::make_unique<pci::Bus>(parent, &pciroot, info, std::move(ecam));
  if (!bus) {
    zxlogf(ERROR, "failed to allocate bus object");
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = bus->DdkAdd(ddk::DeviceAddArgs("bus").set_inspect_vmo(bus->GetInspectVmo()))) !=
      ZX_OK) {
    zxlogf(ERROR, "failed to add bus driver: %s", zx_status_get_string(status));
    return status;
  }

  if ((status = bus->Initialize()) != ZX_OK) {
    zxlogf(ERROR, "failed to initialize driver: %s", zx_status_get_string(status));
    bus->DdkAsyncRemove();
    return status;
  }

  // The DDK owns the object if we've mae it this far.
  (void)bus.release();
  return ZX_OK;
}

zx_status_t Bus::Initialize() {
  // Stash the ops/ctx pointers for the pciroot protocol so we can pass
  // them to the allocators provided by Pci(e)Root. The initial root is
  // created to manage the start of the bus id range given to use by the
  // pciroot protocol.
  root_ = std::unique_ptr<PciRoot>(new PciRoot(info_.start_bus_num, pciroot_));

  // Begin our bus scan starting at our root
  ScanDownstream();
  zx_status_t status = ConfigureLegacyIrqs();
  if (status != ZX_OK) {
    zxlogf(ERROR, "error configuring legacy IRQs, they will be unavailable: %s",
           zx_status_get_string(status));
    return status;
  }
  root_->ConfigureDownstreamDevices();
  StartIrqWorker();

  return ZX_OK;
}

// Maps a vmo as an mmio_buffer to be used as this Bus driver's ECAM region
// for config space access.
zx::status<ddk::MmioBuffer> Bus::MapEcam(zx::vmo ecam_vmo) {
  size_t size;
  zx_status_t status = ecam_vmo.get_size(&size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "couldn't get ecam vmo size: %d!", status);
    return zx::error(status);
  }

  std::optional<ddk::MmioBuffer> ecam = {};
  status =
      ddk::MmioBuffer::Create(0, size, std::move(ecam_vmo), ZX_CACHE_POLICY_UNCACHED_DEVICE, &ecam);
  if (status != ZX_OK) {
    zxlogf(ERROR, "couldn't map ecam vmo: %d!", status);
    return zx::error(status);
  }

  zxlogf(DEBUG, "ecam for mapped at %p (size: %#zx)", ecam->get(), ecam->get_size());
  return zx::ok(std::move(*ecam));
}

zx_status_t Bus::MakeConfig(pci_bdf_t bdf, std::unique_ptr<Config>* out_config) {
  zx_status_t status;
  if (ecam_) {
    status = MmioConfig::Create(bdf, &(*ecam_), info_.start_bus_num, info_.end_bus_num, out_config);
  } else {
    status = ProxyConfig::Create(bdf, &pciroot_, out_config);
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to create config for %02x:%02x.%1x: %d!", bdf.bus_id, bdf.device_id,
           bdf.function_id, status);
  }

  return status;
}

// Scan downstream starting at the bus id managed by the Bus's Root.
// In the process of scanning, take note of bridges found and configure any that are
// unconfigured. In the end the Bus should have a list of all devides, and all bridges
// should have a list of pointers to their own downstream devices.
zx_status_t Bus::ScanDownstream() {
  std::list<BusScanEntry> scan_list;
  // First scan the bus id associated with our root.
  BusScanEntry entry = {{static_cast<uint8_t>(root_->managed_bus_id()), 0, 0}, root_.get()};
  entry.upstream = root_.get();
  scan_list.push_back(entry);

  // Process any bridges found under the root, any bridges under those bridges, etc...
  // It's important that we scan in the order we discover bridges (DFS) because
  // when we implement bus id assignment it will affect the overall numbering
  // scheme of the bus topology.
  while (!scan_list.empty()) {
    auto entry = scan_list.back();
    zxlogf(TRACE, "scanning from %02x:%02x.%01x upstream: %s", entry.bdf.bus_id,
           entry.bdf.device_id, entry.bdf.function_id,
           (entry.upstream->type() == UpstreamNode::Type::ROOT)
               ? "root"
               : static_cast<Bridge*>(entry.upstream)->config()->addr());
    // Remove this entry, otherwise we'll pop the wrong child off if the scan
    // adds any new bridges / resume points.
    scan_list.pop_back();
    ScanBus(entry, &scan_list);
  }

  return ZX_OK;
}

void Bus::ScanBus(BusScanEntry entry, std::list<BusScanEntry>* scan_list) {
  uint32_t bus_id = entry.bdf.bus_id;  // 32bit so bus_id won't overflow 8bit in the loop
  uint8_t _dev_id = entry.bdf.device_id;
  uint8_t _func_id = entry.bdf.function_id;
  UpstreamNode* upstream = entry.upstream;
  for (uint8_t dev_id = _dev_id; dev_id < PCI_MAX_DEVICES_PER_BUS; dev_id++) {
    for (uint8_t func_id = _func_id; func_id < PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
      std::unique_ptr<Config> config;
      pci_bdf_t bdf = {static_cast<uint8_t>(bus_id), dev_id, func_id};
      zx_status_t status = MakeConfig(bdf, &config);
      if (status != ZX_OK) {
        continue;
      }

      // Check that the device is valid by verifying the vendor and device ids.
      if (config->Read(Config::kVendorId) == PCI_INVALID_VENDOR_ID) {
        continue;
      }

      bool is_bridge =
          ((config->Read(Config::kHeaderType) & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE);
      zxlogf(TRACE, "\tfound %s at %02x:%02x.%1x", (is_bridge) ? "bridge" : "device", bus_id,
             dev_id, func_id);

      inspect::Node node = inspector_.GetRoot().CreateChild(config->addr());
      // If we found a bridge, add it to our bridge list and initialize /
      // enumerate it after we finish scanning this bus
      if (is_bridge) {
        fbl::RefPtr<Bridge> bridge;
        uint8_t mbus_id = config->Read(Config::kSecondaryBusId);
        status = Bridge::Create(zxdev(), std::move(config), upstream, this, std::move(node),
                                mbus_id, &bridge);
        if (status != ZX_OK) {
          zxlogf(ERROR, "failed to create Bridge at %s: %s", config->addr(),
                 zx_status_get_string(status));
          continue;
        }

        // Create scan entries for the next device we would have looked
        // at in the current level of the tree, as well as the new
        // bridge. Since we always work our way from the top of the scan
        // stack we effectively scan the bus in a DFS manner. |func_id|
        // is always incremented by one to ensure we don't scan this
        // same bdf again. If the incremented value is invalid then the
        // device_id loop will iterate and we'll be in a good state
        // again.
        BusScanEntry resume_entry{};
        resume_entry.bdf.bus_id = static_cast<uint8_t>(bus_id);
        resume_entry.bdf.device_id = dev_id;
        resume_entry.bdf.function_id = static_cast<uint8_t>(func_id + 1);
        resume_entry.upstream = upstream;  // Same upstream as this scan
        scan_list->push_back(resume_entry);

        BusScanEntry bridge_entry{};
        bridge_entry.bdf.bus_id = static_cast<uint8_t>(bridge->managed_bus_id());
        bridge_entry.upstream = bridge.get();  // the new bridge will be this scan's upstream
        scan_list->push_back(bridge_entry);
        // Quit this scan and pick up again based on the scan entries found.
        return;
      }

      // We're at a leaf node in the topology so create a normal device
      status = pci::Device::Create(zxdev(), std::move(config), upstream, this, std::move(node));
      if (status != ZX_OK) {
        zxlogf(ERROR, "failed to create device at %s: %s", config->addr(),
               zx_status_get_string(status));
      }
    }

    // Reset _func_id to zero here so that after we resume a single function
    // scan we'll be able to scan the full 8 functions of a given device.
    _func_id = 0;
  }
}

zx_status_t Bus::SetUpLegacyIrqHandlers() {
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &legacy_irq_port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to create IRQ port: %s", zx_status_get_string(status));
    return status;
  }

  // most cases they'll be using MSI / MSI-X anyway so a warning is sufficient.
  fbl::Span<const pci_legacy_irq> irqs(info_.legacy_irqs_list, info_.legacy_irqs_count);
  for (auto& irq : irqs) {
    zx::interrupt interrupt(irq.interrupt);
    status = interrupt.bind(legacy_irq_port_, irq.vector, ZX_INTERRUPT_BIND);
    if (status != ZX_OK) {
      // In most cases a function will use MSI or MSI-X so a warning is sufficient.
      zxlogf(WARNING, "failed to bind irq %#x to port: %s", irq.vector,
             zx_status_get_string(status));
      return status;
    }

    auto shared_vector = std::make_unique<SharedVector>();
    shared_vector->interrupt = std::move(interrupt);

    // Every vector has a list of devices associated with it that are wired to that IRQ.
    shared_irqs_[irq.vector] = std::move(shared_vector);
  }

  return ZX_OK;
}

zx_status_t Bus::ConfigureLegacyIrqs() {
  fbl::AutoLock devices_lock(&devices_lock_);
  zx_status_t status = SetUpLegacyIrqHandlers();
  if (status != ZX_OK) {
    return status;
  }

  // Scan all the devices found and figure out their interrupt pin based on the
  // routing table provided by the platform. While we hold the devices_lock no
  // changes can be made to the Bus topology, ensuring the lifetimes of the
  // upstream paths and config accesses.
  fbl::Span<const pci_irq_routing_entry_t> routing_entries(info_.irq_routing_list,
                                                           info_.irq_routing_count);
  for (auto& device : devices_) {
    uint8_t pin = device.config()->Read(Config::kInterruptPin);
    // If a device has no pin configured in the InterruptPin register then it
    // has no legacy interrupt. PCI Local Bus Spec v3 Section 2.2.6.
    if (pin == 0) {
      continue;
    }

    // To avoid devices all ending up on the same pin the PCI bridge spec
    // defines a transformation per pin based on the device id of a given function
    // and pin. This transformation is applied at every transition from a
    // secondary bus to a primary bus up to the root. In effect, we swizzle the
    // pin every time we find a bridge working our way back up to the root. At
    // the same time, we also want to record the bridge closest to the root in
    // case it is a root port so that we can check the correct irq routing table
    // entries.
    //
    // Pci Bridge to Bridge spec r1.2 Table 9-1
    // PCI Express Base Specification r4.0 Table 2-19
    UpstreamNode* upstream = device.upstream();
    std::optional<pci_bdf_t> port;
    while (upstream && upstream->type() == UpstreamNode::Type::BRIDGE) {
      pin = (pin + device.dev_id()) % PCI_MAX_LEGACY_IRQ_PINS;
      auto bridge = static_cast<pci::Bridge*>(upstream);
      port = bridge->config()->bdf();
      upstream = bridge->upstream();
    }
    ZX_DEBUG_ASSERT(upstream);
    ZX_DEBUG_ASSERT(upstream->type() == UpstreamNode::Type::ROOT);

    // If we didn't find a parent then the device must be a root complex endpoint.
    if (!port) {
      port = {.device_id = PCI_IRQ_ROUTING_NO_PARENT, .function_id = PCI_IRQ_ROUTING_NO_PARENT};
    }

    // There must be a routing entry for a given device / root port combination
    // in order for a device's legacy IRQ to work. Attempt to find it and use
    // the newly swizzled pin value to find the hardware vector.
    auto find_fn = [&device, port](auto& entry) -> bool {
      return entry.port_device_id == port->device_id &&
             entry.port_function_id == port->function_id && entry.device_id == device.dev_id();
    };

    auto found = std::find_if(routing_entries.begin(), routing_entries.end(), find_fn);
    if (found != std::end(routing_entries)) {
      uint8_t vector = found->pins[pin - 1];
      device.config()->Write(Config::kInterruptLine, vector);
      zxlogf(DEBUG, "[%s] pin %u mapped to %#x", device.config()->addr(), pin, vector);
    } else {
      zxlogf(DEBUG, "[%s] no legacy routing entry found for device", device.config()->addr());
    }
  }

  return ZX_OK;
}

Bus::~Bus() {
  ZX_DEBUG_ASSERT(root_);
  root_->DisableDownstream();
  root_->UnplugDownstream();
  zx_status_t status = StopIrqWorker();
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to stop the irq thread: %s", zx_status_get_string(status));
  }
  irq_thread_.join();
}

void Bus::StartIrqWorker() {
  std::thread worker(LegacyIrqWorker, std::ref(legacy_irq_port_), &devices_lock_, &shared_irqs_);
  irq_thread_ = std::move(worker);
}

zx_status_t Bus::StopIrqWorker() {
  zx_port_packet_t packet = {.type = ZX_PKT_TYPE_USER};
  return legacy_irq_port_.queue(&packet);
}

void Bus::LegacyIrqWorker(const zx::port& port, fbl::Mutex* lock, SharedIrqMap* shared_irq_map) {
  zxlogf(TRACE, "IRQ worker started");
  zx_port_packet_t packet = {};
  zx_status_t status = ZX_OK;
  do {
    status = port.wait(zx::time::infinite(), &packet);
    if (status == ZX_OK && packet.status == ZX_OK) {
      // Signal to exit the IRQ thread
      if (packet.type == ZX_PKT_TYPE_USER) {
        return;
      }

      ZX_DEBUG_ASSERT(packet.type == ZX_PKT_TYPE_INTERRUPT);
      // This is effectively our 'fast path'. We've received an interrupt packet
      // and we need to scan the list for devices mapped to that vector to see
      // which ones have an interrupt asserted in their status register. In a
      // typical situation a bus driver is required to check if a driver has
      // interrupts enabled and if the status bit is asserted. However, in our
      // case if a device exists in this list it was only through enabling
      // legacy IRQs, ensuring that interrupts are enabled. We can save a config
      // read and just check status thanks to this.
      fbl::AutoLock devices_lock(lock);
      auto& vector = packet.key;
      auto result = shared_irq_map->find(vector);
      auto& [interrupt, list] = *result->second;
      ZX_DEBUG_ASSERT(result != shared_irq_map->end());
      for (auto& device : list) {
        fbl::AutoLock device_lock(device.dev_lock());
        config::Status status = {.value = device.config()->Read(Config::kStatus)};
        if (status.interrupt_status()) {
          // Trigger the virtual interrupt the device driver is using by proxy.
          zx_status_t signal_status = device.SignalLegacyIrq(packet.interrupt.timestamp);
          if (signal_status != ZX_OK) {
            zxlogf(ERROR, "failed to signal vector %#lx for device %s: %s", vector,
                   device.config()->addr(), zx_status_get_string(signal_status));
          }

          // In the case of PCI_IRQ_MODE_LEGACY, disable the legacy interrupt on
          // a device until a driver services and acknowledges it. If we're in
          // the NOACK mode then we update the running total we keep of
          // interrupts per period. If they exceed the configured limit then
          // then the interrupt will be disabled. In that case, the device has
          // no way to re-enable it without changing IRQ modes.
          auto& irqs = device.irqs();
          bool disable_irq = true;
          if (irqs.mode == PCI_IRQ_MODE_LEGACY_NOACK) {
            irqs.irqs_in_period++;
            if (packet.interrupt.timestamp - irqs.legacy_irq_period_start >= kLegacyNoAckPeriod) {
              irqs.legacy_irq_period_start = packet.interrupt.timestamp;
              irqs.irqs_in_period = 1;
            }

            if (irqs.irqs_in_period < kMaxIrqsPerNoAckPeriod) {
              disable_irq = false;
            }
          }

          if (disable_irq) {
            device.DisableLegacyIrq();
          }
        }
      }

      // Re-arm the given interrupt now that all the devices have been checked.
      status = interrupt.ack();
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to ack vector %#lx after servicing device: %s", vector,
               zx_status_get_string(status));
      }
    } else {
      zxlogf(ERROR, "Unexpected error in IRQ handling, status = %s, pkt status = %s",
             zx_status_get_string(status), zx_status_get_string(packet.status));
    }
  } while (status == ZX_OK && packet.status == ZX_OK);
}

}  // namespace pci
