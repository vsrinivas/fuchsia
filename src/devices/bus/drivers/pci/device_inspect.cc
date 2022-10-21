// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/ddk/debug.h>

#include <fbl/string_buffer.h>
#include <pretty/sizes.h>

#include "src/devices/bus/drivers/pci/config.h"
#include "src/devices/bus/drivers/pci/device.h"

namespace pci {

void Device::InspectUpdateInterrupts() {
  // In most cases we can just have Inspect handle the storage for these nodes because we don't need
  // to modify them after creation.
  inspect_.interrupts = inspect_.device.CreateChild(Inspect::kInspectHeaderInterrupts);
  inspect_.interrupts.RecordString(Inspect::kInspectIrqMode, Inspect::kInspectIrqModes[irqs_.mode]);
  switch (irqs_.mode) {
    case PCI_INTERRUPT_MODE_LEGACY:
      inspect_.legacy_signal_cnt =
          inspect_.interrupts.CreateUint(Inspect::kInspectLegacySignalCount, 0);
      inspect_.legacy_ack_cnt = inspect_.interrupts.CreateUint(Inspect::kInspectLegacyAckCount, 0);
      __FALLTHROUGH;
    case PCI_INTERRUPT_MODE_LEGACY_NOACK: {
      char s[2] = {static_cast<char>('A' + (irqs_.legacy_pin - 1)), '\0'};
      inspect_.interrupts.RecordString(Inspect::kInspectLegacyInterruptPin, s);
      inspect_.interrupts.RecordUint(Inspect::kInspectLegacyInterruptLine, irqs_.legacy_vector);
      break;
    }
    case PCI_INTERRUPT_MODE_MSI:
    case PCI_INTERRUPT_MODE_MSI_X: {
      zx_info_msi_t info{};
      const zx_status_t status =
          irqs_.msi_allocation.get_info(ZX_INFO_MSI, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        zxlogf(WARNING, "Unable to look up MSI diagnostic information: %s",
               zx_status_get_string(status));
        break;
      }
      inspect_.interrupts.RecordUint(Inspect::kInspectMsiBaseVector, info.base_irq_id);
      inspect_.interrupts.RecordUint(Inspect::kInspectMsiAllocated, info.num_irq);
      // We don't include mapped information here because it's not possible to
      // have the correct information without lazy node support. For instance,
      // if a driver closes the mapped interrupt handle then we would have no
      // way to know to update the inspect information.
      break;
    }
  }
}

void Device::InspectIncrementLegacySignalCount() { inspect_.legacy_signal_cnt.Add(1); }
void Device::InspectIncrementLegacyAckCount() { inspect_.legacy_ack_cnt.Add(1); }

// Get or create optional nodes as necessary. Doing it in this manner appeasers
// the linter which wants to see that the status of optionals is explicitly
// checked in callers.
inspect::Node& Device::InspectGetOrCreateBarNode(uint8_t bar_id) {
  if (!inspect_.bar) {
    inspect_.bar = inspect_.device.CreateChild(Inspect::kInspectHeaderBars);
  }

  if (!inspect_.bars[bar_id]) {
    const char name[] = {static_cast<char>('0' + bar_id), '\0'};
    inspect_.bars[bar_id] = inspect_.bar.CreateChild(name);
  }

  return inspect_.bars[bar_id].value();
}

void Device::InspectRecordBarState(const char* name, uint8_t bar_id, uint64_t bar_val) {
  std::array<char, 16> value = {};
  snprintf(value.data(), value.max_size(), "0x%lx", bar_val);
  InspectGetOrCreateBarNode(bar_id).RecordString(name, value.data());
}

void Device::InspectRecordBarInitialState(uint8_t bar_id, uint64_t bar_val) {
  InspectRecordBarState(Inspect::kInspectHeaderBarsInitial, bar_id, bar_val);
}

void Device::InspectRecordBarConfiguredState(uint8_t bar_id, uint64_t bar_val) {
  InspectRecordBarState(Inspect::kInspectHeaderBarsConfigured, bar_id, bar_val);
}

void Device::InspectRecordBarProbedState(uint8_t bar_id, const Bar& bar) {
  std::array<char, 128> value = {};
  std::array<char, 8> pretty_size = {};
  snprintf(value.data(), value.max_size(), "%s (%s%sprefetchable) [size=%s]",
           (bar.is_mmio) ? "MMIO" : "IO", (bar.is_64bit) ? "64-bit, " : "",
           (bar.is_prefetchable) ? "" : "non-",
           format_size(pretty_size.data(), pretty_size.max_size(), bar.size));
  InspectGetOrCreateBarNode(bar_id).RecordString(Inspect::kInspectHeaderBarsProbed, value.data());
}

void Device::InspectRecordBarRange(const char* name, uint8_t bar_id, ralloc_region_t region) {
  std::array<char, 64> value = {};
  snprintf(value.data(), value.max_size(), "[%#lx, %#lx) %#lx", region.base,
           region.base + region.size, region.size);
  InspectGetOrCreateBarNode(bar_id).RecordString(name, value.data());
}

void Device::InspectRecordBarFailure(uint8_t bar_id, ralloc_region_t region) {
  InspectRecordBarRange(Inspect::kInspectHeaderBarsFailed, bar_id, region);
}

void Device::InspectRecordBarReallocation(uint8_t bar_id, ralloc_region_t region) {
  InspectRecordBarRange(Inspect::kInspectHeaderBarsReallocated, bar_id, region);
}

}  // namespace pci
