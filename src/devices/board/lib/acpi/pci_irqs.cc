// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <assert.h>
#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <lib/pci/pciroot.h>
#include <stdio.h>
#include <zircon/hw/pci.h>

#include <array>
#include <cstring>
#include <optional>

#include <acpica/acpi.h>

#include "src/devices/board/lib/acpi/pci-internal.h"
#include "src/devices/board/lib/acpi/status.h"
#include "src/devices/board/lib/acpi/util.h"

// Legacy PCI device functions have a single interrupt that was traditionally
// wired directly into the interrupt controller. There are only four interrupt
// lines shared among devices, labeled A through D. When an interrupt is
// triggered on one of these lines it's the responsibility of system software to
// look at all devices using the line wired to that vector and check which
// device has their interrupt status bit flipped. To properly configure these
// legacy interrupts at the platform level we need to read the PCI Routing
// Tables (_PRT) for each root port found. PCI Routing Tables represent a
// mapping between a root device/function adddress and an Interrupt Link Device
// (ILD) or hardware vector. This ILD contains a resource that details how it is
// wired up, and how the interrupt needs to be configured. Using this we can
// build a routing table between a given BDF pin and a hard vector in the bus
// driver.

namespace {

struct PortInfo {
  uint8_t dev_id;
  uint8_t func_id;
};

constexpr const char* im_to_str(uint32_t irq_mode) {
  switch (irq_mode) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
      return "edge triggered, active low";
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
      return "edge triggered, active high";
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
      return "level triggered, active high";
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
      return "level triggered, active low";
    default:
      return "<unsupported irq mode>";
  }
}

// Find Extended IRQ information for a PRT's Interrupt Link Device.
acpi::status<ACPI_RESOURCE_EXTENDED_IRQ> FindExtendedIrqResource(ACPI_HANDLE parent,
                                                                 char source[4]) {
  // If this method is called then we're attempting to find the Interrupt Link Device referenced by
  // a given PRT entry.
  ACPI_HANDLE ild;
  ACPI_STATUS status = AcpiGetHandle(parent, source, &ild);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  acpi::AcpiBuffer<ACPI_RESOURCE> crs_buffer;
  status = AcpiGetCurrentResources(ild, &crs_buffer);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  for (auto& res : crs_buffer) {
    if (res.Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
      return acpi::ok(res.Data.ExtendedIrq);
    }
  }
  return acpi::error(AE_NOT_FOUND);
}

// Take a PRT entry and return a usable acpi_legacy_irq based on the type of IRQ
// source information we were able to find in the ACPI table.
acpi_legacy_irq PrtEntryToIrq(ACPI_HANDLE object, ACPI_PCI_ROUTING_TABLE* entry) {
  // By default, SourceIndex refers to a global IRQ number that the pin is
  // connected to and we assume the legacy defaults of Level-triggered / Active Low.
  // PCI Local Bus Specification 3.0 section 2.2.6
  acpi_legacy_irq new_irq = {.vector = entry->SourceIndex, .options = ZX_INTERRUPT_MODE_LEVEL_LOW};
  // If the PRT contains a Source entry than we can attempt to find an Extended
  // IRQ Resource describing it.
  if (entry->Source[0]) {
    if (auto result = FindExtendedIrqResource(object, entry->Source); result.is_ok()) {
      new_irq.vector = result->Interrupts[0];
      if (result->Triggering == ACPI_LEVEL_SENSITIVE) {
        if (result->Polarity == ACPI_ACTIVE_HIGH) {
          new_irq.options = ZX_INTERRUPT_MODE_LEVEL_HIGH;
        } else {
          new_irq.options = ZX_INTERRUPT_MODE_LEVEL_LOW;
        }
      } else {
        if (result->Polarity == ACPI_ACTIVE_HIGH) {
          new_irq.options = ZX_INTERRUPT_MODE_EDGE_HIGH;
        } else {
          new_irq.options = ZX_INTERRUPT_MODE_EDGE_LOW;
        }
      }
    }
  }

  return new_irq;
}

void AddIrqToAccounting(acpi_legacy_irq irq, AcpiPciroot::Context* context,
                        ACPI_PCI_ROUTING_TABLE* entry, std::optional<PortInfo> port,
                        uint8_t local_dev_id) {
  // The first time we find an irq in a PRT it should be stored in the root's
  // context so that later it can be passed to the PCI bus driver.
  auto vector_entry = context->irqs.find(irq.vector);
  if (vector_entry == context->irqs.end()) {
    context->irqs[irq.vector] = irq;
    zxlogf(DEBUG, "added vector %#x { %s } from PRT", irq.vector, im_to_str(irq.options));
  } else if (vector_entry->second.options != irq.options) {
    // This may not be fatal, but it would represent a misconfiguration that
    // would likely result in some devices wired to this pin to have
    // malfunctioning IRQs. It would most likely reflect an error in an ACPI
    // table, but we cannot do much about it without knowing which configuration
    // is correct. In lieu of that, go with the first.
    zxlogf(WARNING, "Multiple IRQ configurations found in PRT for vector %#x!", irq.vector);
  }

  auto& routing_vec = context->routing;
  auto find_fn = [local_dev_id, port](auto& e) -> bool {
    return ((port) ? port->dev_id : PCI_IRQ_ROUTING_NO_PARENT) == e.port_device_id &&
           ((port) ? port->func_id : PCI_IRQ_ROUTING_NO_PARENT) == e.port_function_id &&
           local_dev_id == e.device_id;
  };

  // Lastly, based on the device / function address provided we need to update the routing
  // table to reflect the new information we've found. If we have a valid device / function
  // address then we can update that entry, otherwise a new entry needs to be made for that
  // combination of port and child device. This would be easier in a map, but a vector allows us to
  // directly point to the backing storage in the pciroot protocol implementation.
  if (auto found_entry = std::find_if(routing_vec.begin(), routing_vec.end(), find_fn);
      found_entry != std::end(routing_vec)) {
    found_entry->pins[entry->Pin] = static_cast<uint8_t>(irq.vector);
  } else {
    pci_irq_routing_entry_t new_entry = {
        .port_device_id = (port) ? port->dev_id : static_cast<uint8_t>(PCI_IRQ_ROUTING_NO_PARENT),
        .port_function_id =
            (port) ? port->func_id : static_cast<uint8_t>(PCI_IRQ_ROUTING_NO_PARENT),
        .device_id = local_dev_id,
        .pins = {0},
    };
    new_entry.pins[entry->Pin] = static_cast<uint8_t>(irq.vector);
    routing_vec.push_back(new_entry);
  }
}

ACPI_STATUS ReadPciRoutingTable(ACPI_HANDLE object, AcpiPciroot::Context* context,
                                std::optional<PortInfo> port) {
  acpi::AcpiBuffer<ACPI_PCI_ROUTING_TABLE> irt_buffer;
  ACPI_STATUS status = AcpiGetIrqRoutingTable(object, &irt_buffer);
  if (status != AE_OK) {
    return status;
  }

  for (auto& entry : irt_buffer) {
    if (entry.Pin >= PCI_MAX_LEGACY_IRQ_PINS) {
      zxlogf(ERROR, "PRT entry contains an invalid pin: %#x", entry.Pin);
      return AE_ERROR;
    }

    zxlogf(TRACE,
           "_PRT Entry RootPort %02x.%1x: .Address = 0x%05llx, .Pin = %u, .SourceIndex = %u, "
           ".Source = \"%s\"",
           (port) ? port->dev_id : 0, (port) ? port->func_id : 0, entry.Address, entry.Pin,
           entry.SourceIndex, entry.Source);

    // Per ACPI Spec 6.2.13, all _PRT entries must have a function address of
    // 0xFFFF representing all functions in the device. In effect, this means we
    // only care about the entry's dev id.
    uint8_t dev_id = (entry.Address >> 16) & (PCI_MAX_DEVICES_PER_BUS - 1);
    // Either we're handling the root complex (port_dev_id == UINT8_MAX), or
    // we're handling a root port, and if it's a root port, dev_id should
    // be 0. If not, the entry is strange and we'll warn / skip it.
    if (port && dev_id != 0) {
      zxlogf(WARNING, "PRT entry for root %.*s unexpected contains device address: %#x",
             static_cast<int>(sizeof(context->name)), context->name, dev_id);
      continue;
    }

    acpi_legacy_irq new_irq = PrtEntryToIrq(object, &entry);
    AddIrqToAccounting(new_irq, context, &entry, port, dev_id);
  }

  return AE_OK;
}

}  // namespace

namespace acpi {

ACPI_STATUS GetPciRootIrqRouting(acpi::Acpi* acpi, ACPI_HANDLE root_obj,
                                 AcpiPciroot::Context* context) {
  // Start with the Root's _PRT. The spec requires that one exists.
  ACPI_STATUS status = ReadPciRoutingTable(root_obj, context, std::nullopt);
  if (status != AE_OK) {
    zxlogf(DEBUG, "Couldn't find an IRQ routing table for root %.*s",
           static_cast<int>(sizeof(context->name)), context->name);
    return status;
  }

  // If there are any host bridges / pcie-to-pci bridges or other ports under
  // the root then check them for PRTs as well. This is unncecessary in most
  // configurations.
  ACPI_HANDLE child = nullptr;
  while ((status = AcpiGetNextObject(ACPI_TYPE_DEVICE, root_obj, child, &child)) == AE_OK) {
    if (auto res = acpi->GetObjectInfo(child); res.is_ok()) {
      // If the object we're examining has a PCI address then use that as the
      // basis for the routing table we're inspecting.
      // Format: Acpi 6.1 section 6.1.1 "_ADR (Address)"
      PortInfo port{
          .dev_id = static_cast<uint8_t>((res->Address >> 16) & (PCI_MAX_DEVICES_PER_BUS - 1)),
          .func_id = static_cast<uint8_t>(res->Address & (PCI_MAX_FUNCTIONS_PER_DEVICE - 1)),
      };
      zxlogf(DEBUG, "Processing _PRT for %02x.%1x (%.*s)", port.dev_id, port.func_id, 4,
             reinterpret_cast<char*>(&res->Name));
      // Ignore the return value of this, since if child is not a root port, it
      // will fail and we don't care.
      ReadPciRoutingTable(child, context, port);
    }
  }

  return AE_OK;
}

}  // namespace acpi
