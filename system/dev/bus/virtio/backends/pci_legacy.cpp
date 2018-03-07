// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <hw/inout.h>
#include <inttypes.h>

#include "pci.h"

namespace virtio {

// These require the backend lock to be held due to the value held in
// bar0_base_ rather than anything having to do with the IO writes.
void PciLegacyBackend::IoReadLocked(uint16_t offset, uint8_t* val) TA_REQ(lock_) {
    *val = inp(static_cast<uint16_t>(bar0_base_ + offset));
    zxlogf(SPEW, "%s: IoReadLocked8(%#x) = %#x\n", tag(), offset, *val);
}
void PciLegacyBackend::IoReadLocked(uint16_t offset, uint16_t* val) TA_REQ(lock_) {
    *val = inpw(static_cast<uint16_t>(bar0_base_ + offset));
    zxlogf(SPEW, "%s: IoReadLocked16(%#x) = %#x\n", tag(), offset, *val);
}
void PciLegacyBackend::IoReadLocked(uint16_t offset, uint32_t* val) TA_REQ(lock_) {
    *val = inpd(static_cast<uint16_t>(bar0_base_ + offset));
    zxlogf(SPEW, "%s: IoReadLocked32(%#x) = %#x\n", tag(), offset, *val);
}
void PciLegacyBackend::IoWriteLocked(uint16_t offset, uint8_t val) TA_REQ(lock_) {
    outp(static_cast<uint16_t>(bar0_base_ + offset), val);
    zxlogf(SPEW, "%s: IoWriteLocked8(%#x) = %#x\n", tag(), offset, val);
}
void PciLegacyBackend::IoWriteLocked(uint16_t offset, uint16_t val) TA_REQ(lock_) {
    outpw(static_cast<uint16_t>(bar0_base_ + offset), val);
    zxlogf(SPEW, "%s: IoWriteLocked16(%#x) = %#x\n", tag(), offset, val);
}
void PciLegacyBackend::IoWriteLocked(uint16_t offset, uint32_t val) TA_REQ(lock_) {
    outpd(static_cast<uint16_t>(bar0_base_ + offset), val);
    zxlogf(SPEW, "%s: IoWriteLocked32(%#x) = %#x\n", tag(), offset, val);
}

zx_status_t PciLegacyBackend::Init() {
    fbl::AutoLock lock(&lock_);
    zx_pci_bar_t bar0;
    zx_status_t status = pci_get_bar(&pci_, 0u, &bar0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Couldn't get IO bar for device: %d\n", tag(), status);
        return status;
    }

    if (bar0.type != PCI_BAR_TYPE_PIO) {
        return ZX_ERR_WRONG_TYPE;
    }

    bar0_base_ = static_cast<uint16_t>(bar0.addr & 0xffff);
    // TODO(cja): When MSI support is added we need to dynamically add
    // the extra two fields here that offset the device config.
    // Virtio 1.0 section 4.1.4.8
    device_cfg_offset_ = VIRTIO_PCI_CONFIG_OFFSET_NOMSIX;
    zxlogf(INFO, "%s: %02x:%02x.%01x using legacy backend (io base %#04x, "
                 "io size: %#04zx, device base %#04x\n", tag(), info_.bus_id,
                 info_.dev_id, info_.func_id, bar0_base_, bar0.size, device_cfg_offset_);
    return ZX_OK;
}

PciLegacyBackend::~PciLegacyBackend() {
    fbl::AutoLock lock(&lock_);
    bar0_base_ = 0;
    device_cfg_offset_ = 0;
}

// value pointers are used to maintain type safety with field width
void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint8_t* value) {
    fbl::AutoLock lock(&lock_);
    IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint16_t* value) {
    fbl::AutoLock lock(&lock_);
    IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint32_t* value) {
    fbl::AutoLock lock(&lock_);
    IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint64_t* value) {
    fbl::AutoLock lock(&lock_);
    auto val = reinterpret_cast<uint32_t*>(value);

    IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), &val[0]);
    IoReadLocked(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)), &val[1]);
}

void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint8_t value) {
    fbl::AutoLock lock(&lock_);
    IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint16_t value) {
    fbl::AutoLock lock(&lock_);
    IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint32_t value) {
    fbl::AutoLock lock(&lock_);
    IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}
void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint64_t value) {
    fbl::AutoLock lock(&lock_);
    auto words = reinterpret_cast<uint32_t*>(&value);
    IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset), words[0]);
    IoWriteLocked(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)), words[1]);
}

// Get the ring size of a specific index
uint16_t PciLegacyBackend::GetRingSize(uint16_t index) {
    fbl::AutoLock lock(&lock_);
    uint16_t val;
    IoWriteLocked(VIRTIO_PCI_QUEUE_SELECT, index);
    IoReadLocked(VIRTIO_PCI_QUEUE_SIZE, &val);
    zxlogf(SPEW, "%s: ring %u size = %u\n", tag(), index, val);
    return val;
}

// Set up ring descriptors with the backend.
void PciLegacyBackend::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc,
                               zx_paddr_t pa_avail, zx_paddr_t pa_used) {
    fbl::AutoLock lock(&lock_);
    // Virtio 1.0 section 2.4.2
    IoWriteLocked(VIRTIO_PCI_QUEUE_SELECT, index);
    IoWriteLocked(VIRTIO_PCI_QUEUE_SIZE, count);
    IoWriteLocked(VIRTIO_PCI_QUEUE_PFN, static_cast<uint32_t>(pa_desc / 4096));
    zxlogf(SPEW, "%s: set ring %u (# = %u, addr = %#lx)\n", tag(), index, count, pa_desc);
}

void PciLegacyBackend::RingKick(uint16_t ring_index) {
    fbl::AutoLock lock(&lock_);
    IoWriteLocked(VIRTIO_PCI_QUEUE_NOTIFY, ring_index);
    zxlogf(SPEW, "%s: kicked ring %u\n", tag(), ring_index);
}

bool PciLegacyBackend::ReadFeature(uint32_t feature) {
    // Legacy PCI back-end can only support one feature word.
    if (feature >= 32) {
        return false;
    }

    fbl::AutoLock lock(&lock_);
    uint32_t val;

    ZX_DEBUG_ASSERT((feature & (feature - 1)) == 0);
    IoReadLocked(VIRTIO_PCI_DEVICE_FEATURES, &val);
    bool is_set = (val & (1u << feature)) > 0;
    zxlogf(SPEW, "%s: read feature bit %u = %u\n", tag(), feature, is_set);
    return is_set;
}

void PciLegacyBackend::SetFeature(uint32_t feature) {
    // Legacy PCI back-end can only support one feature word.
    if (feature >= 32) {
        return;
    }

    fbl::AutoLock lock(&lock_);
    uint32_t val;
    ZX_DEBUG_ASSERT((feature & (feature - 1)) == 0);
    IoReadLocked(VIRTIO_PCI_DRIVER_FEATURES, &val);
    IoWriteLocked(VIRTIO_PCI_DRIVER_FEATURES, val | (1u << feature));
    zxlogf(SPEW, "%s: feature bit %u now set\n", tag(), feature);
}

// Virtio v0.9.5 does not support the FEATURES_OK negotiation so this should
// always succeed.
zx_status_t PciLegacyBackend::ConfirmFeatures() {
    return ZX_OK;
}

void PciLegacyBackend::DeviceReset() {
    fbl::AutoLock lock(&lock_);
    IoWriteLocked(VIRTIO_PCI_DEVICE_STATUS, 0u);
    zxlogf(SPEW, "%s: device reset\n", tag());
}

void PciLegacyBackend::SetStatusBits(uint8_t bits) {
    fbl::AutoLock lock(&lock_);
    uint8_t status;
    IoReadLocked(VIRTIO_PCI_DEVICE_STATUS, &status);
    IoWriteLocked(VIRTIO_PCI_DEVICE_STATUS, static_cast<uint8_t>(status | bits));
}

void PciLegacyBackend::DriverStatusAck() {
    SetStatusBits(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    zxlogf(SPEW, "%s: driver acknowledge\n", tag());
}

void PciLegacyBackend::DriverStatusOk() {
    SetStatusBits(VIRTIO_STATUS_DRIVER_OK);
    zxlogf(SPEW, "%s: driver ok\n", tag());
}

uint32_t PciLegacyBackend::IsrStatus() {
    fbl::AutoLock lock(&lock_);
    uint8_t isr_status;
    IoReadLocked(VIRTIO_PCI_ISR_STATUS, &isr_status);
    return isr_status & (VIRTIO_ISR_QUEUE_INT | VIRTIO_ISR_DEV_CFG_INT);
}

} // namespace virtio
