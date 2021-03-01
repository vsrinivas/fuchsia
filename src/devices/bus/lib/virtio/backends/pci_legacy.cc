// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <hw/inout.h>

#include "../include/lib/virtio/backends/pci.h"

namespace virtio {

// These require the backend lock to be held due to the value held in
// bar0_base_ rather than anything having to do with the IO writes.
void PciLegacyBackend::IoReadLocked(uint16_t offset, uint8_t* val) {
  *val = inp(static_cast<uint16_t>(bar0_base_ + offset));
  zxlogf(TRACE, "%s: IoReadLocked8(%#x) = %#x", tag(), offset, *val);
}
void PciLegacyBackend::IoReadLocked(uint16_t offset, uint16_t* val) {
  *val = inpw(static_cast<uint16_t>(bar0_base_ + offset));
  zxlogf(TRACE, "%s: IoReadLocked16(%#x) = %#x", tag(), offset, *val);
}
void PciLegacyBackend::IoReadLocked(uint16_t offset, uint32_t* val) {
  *val = inpd(static_cast<uint16_t>(bar0_base_ + offset));
  zxlogf(TRACE, "%s: IoReadLocked32(%#x) = %#x", tag(), offset, *val);
}
void PciLegacyBackend::IoWriteLocked(uint16_t offset, uint8_t val) {
  outp(static_cast<uint16_t>(bar0_base_ + offset), val);
  zxlogf(TRACE, "%s: IoWriteLocked8(%#x) = %#x", tag(), offset, val);
}
void PciLegacyBackend::IoWriteLocked(uint16_t offset, uint16_t val) {
  outpw(static_cast<uint16_t>(bar0_base_ + offset), val);
  zxlogf(TRACE, "%s: IoWriteLocked16(%#x) = %#x", tag(), offset, val);
}
void PciLegacyBackend::IoWriteLocked(uint16_t offset, uint32_t val) {
  outpd(static_cast<uint16_t>(bar0_base_ + offset), val);
  zxlogf(TRACE, "%s: IoWriteLocked32(%#x) = %#x", tag(), offset, val);
}

zx_status_t PciLegacyBackend::Init() {
  fbl::AutoLock guard(&lock());
  pci_bar_t bar0;
  zx_status_t status = pci().GetBar(0u, &bar0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Couldn't get IO bar for device: %s", tag(), zx_status_get_string(status));
    return status;
  }

  if (bar0.type != ZX_PCI_BAR_TYPE_PIO) {
    return ZX_ERR_WRONG_TYPE;
  }

  bar0_base_ = static_cast<uint16_t>(bar0.u.addr & 0xffff);
  // TODO(cja): When MSI support is added we need to dynamically add
  // the extra two fields here that offset the device config.
  // Virtio 1.0 section 4.1.4.8
  device_cfg_offset_ = VIRTIO_PCI_CONFIG_OFFSET_NOMSIX;
  zxlogf(INFO,
         "%s: %02x:%02x.%01x using legacy backend (io base %#04x, "
         "io size: %#04zx, device base %#04x\n",
         tag(), info().bus_id, info().dev_id, info().func_id, bar0_base_, bar0.size,
         device_cfg_offset_);
  return ZX_OK;
}

PciLegacyBackend::~PciLegacyBackend() {
  fbl::AutoLock guard(&lock());
  bar0_base_ = 0;
  device_cfg_offset_ = 0;
}

// value pointers are used to maintain type safety with field width
void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint8_t* value) {
  fbl::AutoLock guard(&lock());
  IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint16_t* value) {
  fbl::AutoLock guard(&lock());
  IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint32_t* value) {
  fbl::AutoLock guard(&lock());
  IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::ReadDeviceConfig(uint16_t offset, uint64_t* value) {
  fbl::AutoLock guard(&lock());
  auto val = reinterpret_cast<uint32_t*>(value);

  IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), &val[0]);
  IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)), &val[1]);
}

void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint8_t value) {
  fbl::AutoLock guard(&lock());
  IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint16_t value) {
  fbl::AutoLock guard(&lock());
  IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint32_t value) {
  fbl::AutoLock guard(&lock());
  IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}
void PciLegacyBackend::WriteDeviceConfig(uint16_t offset, uint64_t value) {
  fbl::AutoLock guard(&lock());
  auto words = reinterpret_cast<uint32_t*>(&value);
  IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), words[0]);
  IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)), words[1]);
}

// Get the ring size of a specific index
uint16_t PciLegacyBackend::GetRingSize(uint16_t index) {
  fbl::AutoLock guard(&lock());
  uint16_t val;
  IoWriteLocked(VIRTIO_PCI_QUEUE_SELECT, index);
  IoReadLocked(VIRTIO_PCI_QUEUE_SIZE, &val);
  zxlogf(TRACE, "%s: ring %u size = %u", tag(), index, val);
  return val;
}

// Set up ring descriptors with the backend.
zx_status_t PciLegacyBackend::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc,
                                      zx_paddr_t pa_avail, zx_paddr_t pa_used) {
  fbl::AutoLock guard(&lock());
  // Virtio 1.0 section 2.4.2
  IoWriteLocked(VIRTIO_PCI_QUEUE_SELECT, index);
  IoWriteLocked(VIRTIO_PCI_QUEUE_SIZE, count);
  IoWriteLocked(VIRTIO_PCI_QUEUE_PFN, static_cast<uint32_t>(pa_desc / 4096));
  zxlogf(TRACE, "%s: set ring %u (# = %u, addr = %#lx)", tag(), index, count, pa_desc);
  return ZX_OK;
}

void PciLegacyBackend::RingKick(uint16_t ring_index) {
  fbl::AutoLock guard(&lock());
  IoWriteLocked(VIRTIO_PCI_QUEUE_NOTIFY, ring_index);
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
  IoReadLocked(VIRTIO_PCI_DEVICE_FEATURES, &val);
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
  IoReadLocked(VIRTIO_PCI_DRIVER_FEATURES, &val);
  IoWriteLocked(VIRTIO_PCI_DRIVER_FEATURES, val | (1u << feature));
  zxlogf(TRACE, "%s: feature bit %u now set", tag(), feature);
}

// Virtio v0.9.5 does not support the FEATURES_OK negotiation so this should
// always succeed.
zx_status_t PciLegacyBackend::ConfirmFeatures() { return ZX_OK; }

void PciLegacyBackend::DeviceReset() {
  fbl::AutoLock guard(&lock());
  IoWriteLocked(VIRTIO_PCI_DEVICE_STATUS, 0u);
  zxlogf(TRACE, "%s: device reset", tag());
}

void PciLegacyBackend::SetStatusBits(uint8_t bits) {
  fbl::AutoLock guard(&lock());
  uint8_t status;
  IoReadLocked(VIRTIO_PCI_DEVICE_STATUS, &status);
  IoWriteLocked(VIRTIO_PCI_DEVICE_STATUS, static_cast<uint8_t>(status | bits));
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
  IoReadLocked(VIRTIO_PCI_ISR_STATUS, &isr_status);
  return isr_status & (VIRTIO_ISR_QUEUE_INT | VIRTIO_ISR_DEV_CFG_INT);
}

}  // namespace virtio
