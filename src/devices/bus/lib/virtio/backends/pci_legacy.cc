// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/inout.h>
#include <zircon/syscalls/types.h>

#include <fbl/auto_lock.h>
#include <virtio/virtio.h>

#include "../include/lib/virtio/backends/pci.h"

namespace virtio {

zx_status_t PciLegacyBackend::Init() {
  fbl::AutoLock guard(&lock());
  pci_bar_t bar0;
  zx_status_t status = pci().GetBar(0u, &bar0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Couldn't get IO bar for device: %s", tag(), zx_status_get_string(status));
    return status;
  }

  if (bar0.type != PCI_BAR_TYPE_IO) {
    return ZX_ERR_WRONG_TYPE;
  }

  bar0_base_ = static_cast<uint16_t>(bar0.result.io.address & std::numeric_limits<uint16_t>::max());

  device_cfg_offset_ =
      bar0_base_ + ((irq_mode() == PCI_INTERRUPT_MODE_MSI_X) ? VIRTIO_PCI_CONFIG_OFFSET_MSIX
                                                             : VIRTIO_PCI_CONFIG_OFFSET_NOMSIX);
  zxlogf(DEBUG, "%s: using legacy backend (io base = %#04x, io size = %#04zx, device base = %#04x)",
         tag(), bar0_base_, bar0.size, device_cfg_offset_);

  return ZX_OK;
}

// value pointers are used to maintain type safety with field width
void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint8_t* value) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Read(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint16_t* value) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Read(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint32_t* value) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Read(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint64_t* value) {
  fbl::AutoLock guard(&lock());
  auto val = reinterpret_cast<uint32_t*>(value);

  legacy_io_->Read(static_cast<uint16_t>(device_cfg_offset_ + offset), &val[0]);
  legacy_io_->Read(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)), &val[1]);
}

void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint8_t value) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Write(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint16_t value) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Write(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint32_t value) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Write(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}
void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint64_t value) {
  fbl::AutoLock guard(&lock());
  auto words = reinterpret_cast<uint32_t*>(&value);
  legacy_io_->Write(static_cast<uint16_t>(device_cfg_offset_ + offset), words[0]);
  legacy_io_->Write(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)),
                    words[1]);
}

// Get the ring size of a specific index
uint16_t PciLegacyBackend::GetRingSize(uint16_t index) {
  fbl::AutoLock guard(&lock());
  uint16_t val;
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_QUEUE_SELECT, index);
  legacy_io_->Read(bar0_base_ + VIRTIO_PCI_QUEUE_SIZE, &val);
  zxlogf(TRACE, "%s: ring %u size = %u", tag(), index, val);
  return val;
}

// Set up ring descriptors with the backend.
zx_status_t PciLegacyBackend::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc,
                                      zx_paddr_t pa_avail, zx_paddr_t pa_used) {
  fbl::AutoLock guard(&lock());
  // Virtio 1.0 section 2.4.2
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_QUEUE_SELECT, index);
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_QUEUE_SIZE, count);
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_QUEUE_PFN, static_cast<uint32_t>(pa_desc / 4096));

  // Virtio 1.0 section 4.1.4.8
  if (irq_mode() == PCI_INTERRUPT_MODE_MSI_X) {
    uint16_t vector = 0;
    legacy_io_->Write(bar0_base_ + VIRTIO_PCI_MSI_CONFIG_VECTOR, PciBackend::kMsiConfigVector);
    legacy_io_->Read(bar0_base_ + VIRTIO_PCI_MSI_CONFIG_VECTOR, &vector);
    if (vector != PciBackend::kMsiConfigVector) {
      zxlogf(ERROR, "MSI-X config vector in invalid state after write: %#x", vector);
      return ZX_ERR_BAD_STATE;
    }

    legacy_io_->Write(bar0_base_ + VIRTIO_PCI_MSI_QUEUE_VECTOR, PciBackend::kMsiQueueVector);
    legacy_io_->Read(bar0_base_ + VIRTIO_PCI_MSI_QUEUE_VECTOR, &vector);
    if (vector != PciBackend::kMsiQueueVector) {
      zxlogf(ERROR, "MSI-X queue vector in invalid state after write: %#x", vector);
      return ZX_ERR_BAD_STATE;
    }
  }

  zxlogf(TRACE, "%s: set ring %u (# = %u, addr = %#lx)", tag(), index, count, pa_desc);
  return ZX_OK;
}

void PciLegacyBackend::RingKick(uint16_t ring_index) {
  fbl::AutoLock guard(&lock());
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_QUEUE_NOTIFY, ring_index);
  zxlogf(TRACE, "%s: kicked ring %u", tag(), ring_index);
}

bool PciLegacyBackend::ReadFeature(uint32_t feature) {
  // Legacy PCI back-end can only support one feature word.
  if (feature >= 32) {
    return false;
  }

  fbl::AutoLock guard(&lock());
  uint32_t val;

  ZX_DEBUG_ASSERT((feature & (feature - 1)) == 0);
  legacy_io_->Read(bar0_base_ + VIRTIO_PCI_DEVICE_FEATURES, &val);
  bool is_set = (val & (1u << feature)) > 0;
  zxlogf(TRACE, "%s: read feature bit %u = %u", tag(), feature, is_set);
  return is_set;
}

void PciLegacyBackend::SetFeature(uint32_t feature) {
  // Legacy PCI back-end can only support one feature word.
  if (feature >= 32) {
    return;
  }

  fbl::AutoLock guard(&lock());
  uint32_t val;
  ZX_DEBUG_ASSERT((feature & (feature - 1)) == 0);
  legacy_io_->Read(bar0_base_ + VIRTIO_PCI_DRIVER_FEATURES, &val);
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_DRIVER_FEATURES, val | (1u << feature));
  zxlogf(TRACE, "%s: feature bit %u now set", tag(), feature);
}

// Virtio v0.9.5 does not support the FEATURES_OK negotiation so this should
// always succeed.
zx_status_t PciLegacyBackend::ConfirmFeatures() { return ZX_OK; }

void PciLegacyBackend::DeviceReset() {
  fbl::AutoLock guard(&lock());
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS, 0u);
  zxlogf(TRACE, "%s: device reset", tag());
}

void PciLegacyBackend::WaitForDeviceReset() {
  fbl::AutoLock guard(&lock());
  uint8_t status = 0xFF;
  while (status != 0) {
    legacy_io_->Read(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS, &status);
  }
  zxlogf(TRACE, "%s: device reset complete", tag());
}

void PciLegacyBackend::SetStatusBits(uint8_t bits) {
  fbl::AutoLock guard(&lock());
  uint8_t status;
  legacy_io_->Read(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS, &status);
  legacy_io_->Write(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS, static_cast<uint8_t>(status | bits));
}

void PciLegacyBackend::DriverStatusAck() {
  SetStatusBits(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
  zxlogf(TRACE, "%s: driver acknowledge", tag());
}

void PciLegacyBackend::DriverStatusOk() {
  SetStatusBits(VIRTIO_STATUS_DRIVER_OK);
  zxlogf(TRACE, "%s: driver ok", tag());
}

uint32_t PciLegacyBackend::IsrStatus() {
  fbl::AutoLock guard(&lock());
  uint8_t isr_status;
  legacy_io_->Read(bar0_base_ + VIRTIO_PCI_ISR_STATUS, &isr_status);
  return isr_status & (VIRTIO_ISR_QUEUE_INT | VIRTIO_ISR_DEV_CFG_INT);
}

}  // namespace virtio
