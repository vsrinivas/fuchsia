// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <inttypes.h>

#include "pci.h"

namespace {

// For reading the virtio specific vendor capabilities that can be PIO or MMIO space
#define cap_field(offset, field) static_cast<uint8_t>(offset + offsetof(virtio_pci_cap_t, field))
static void ReadVirtioCap(pci_protocol_t* pci, uint8_t offset, virtio_pci_cap& cap) {
    pci_config_read8(pci, cap_field(offset, cap_vndr), &cap.cap_vndr);
    pci_config_read8(pci, cap_field(offset, cap_next), &cap.cap_next);
    pci_config_read8(pci, cap_field(offset, cap_len), &cap.cap_len);
    pci_config_read8(pci, cap_field(offset, cfg_type), &cap.cfg_type);
    pci_config_read8(pci, cap_field(offset, bar), &cap.bar);
    pci_config_read32(pci, cap_field(offset, offset), &cap.offset);
    pci_config_read32(pci, cap_field(offset, length), &cap.length);
}
#undef cap_field

// MMIO reads and writes are abstracted out into template methods that
// ensure fields are only accessed with the right size.
template <typename T>
void MmioWrite(volatile T* addr, T value) {
    T::bad_instantiation();
}

template <typename T>
void MmioRead(const volatile T* addr, T* value) {
    T::bad_instantiation();
}

template <>
void MmioWrite<uint32_t>(volatile uint32_t* addr, uint32_t value) {
    writel(value, addr);
}

template <>
void MmioRead<uint32_t>(const volatile uint32_t* addr, uint32_t* value) {
    *value = readl(addr);
}

template <>
void MmioWrite<uint16_t>(volatile uint16_t* addr, uint16_t value) {
    writew(value, addr);
}

template <>
void MmioRead<uint16_t>(const volatile uint16_t* addr, uint16_t* value) {
    *value = readw(addr);
}

template <>
void MmioWrite<uint8_t>(volatile uint8_t* addr, uint8_t value) {
    writeb(value, addr);
}

template <>
void MmioRead<uint8_t>(const volatile uint8_t* addr, uint8_t* value) {
    *value = readb(addr);
}

// Virtio 1.0 Section 4.1.3:
// 64-bit fields are to be treated as two 32-bit fields, with low 32 bit
// part followed by the high 32 bit part.
template <>
void MmioWrite<uint64_t>(volatile uint64_t* addr, uint64_t value) {
    auto words = reinterpret_cast<volatile uint32_t*>(addr);
    MmioWrite(&words[0], static_cast<uint32_t>(value));
    MmioWrite(&words[1], static_cast<uint32_t>(value >> 32));
}

template <>
void MmioRead<uint64_t>(const volatile uint64_t* addr, uint64_t* value) {
    auto words = reinterpret_cast<const volatile uint32_t*>(addr);
    uint32_t lo, hi;
    MmioRead(&words[0], &lo);
    MmioRead(&words[1], &hi);
    *value = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
}

} // anonymous namespace

namespace virtio {

zx_status_t PciModernBackend::Init() {
    fbl::AutoLock lock(&lock_);

    // try to parse capabilities
    for (uint8_t off = pci_get_first_capability(&pci_, kPciCapIdVendor);
         off != 0;
         off = pci_get_next_capability(&pci_, off, kPciCapIdVendor)) {
        virtio_pci_cap_t cap;

        ReadVirtioCap(&pci_, off, cap);
        switch (cap.cfg_type) {
        case VIRTIO_PCI_CAP_COMMON_CFG:
            CommonCfgCallbackLocked(cap);
            break;
        case VIRTIO_PCI_CAP_NOTIFY_CFG:
            // Virtio 1.0 section 4.1.4.4
            // notify_off_multiplier is a 32bit field following this capability
            pci_config_read32(&pci_, static_cast<uint8_t>(off + sizeof(virtio_pci_cap_t)),
                    &notify_off_mul_);
            NotifyCfgCallbackLocked(cap);
            break;
        case VIRTIO_PCI_CAP_ISR_CFG:
            IsrCfgCallbackLocked(cap);
            break;
        case VIRTIO_PCI_CAP_DEVICE_CFG:
            DeviceCfgCallbackLocked(cap);
            break;
        case VIRTIO_PCI_CAP_PCI_CFG:
            PciCfgCallbackLocked(cap);
            break;
        }
    }

    // Ensure we found needed capabilities during parsing
    if (common_cfg_ == nullptr || isr_status_ == nullptr || device_cfg_ == 0 || notify_base_ == 0) {
        zxlogf(ERROR, "%s: failed to bind, missing capabilities\n", tag());
        return ZX_ERR_BAD_STATE;
    }

    zxlogf(SPEW, "virtio: modern pci backend successfully initialized\n");
    return ZX_OK;
}

// value pointers are used to maintain type safety with field width
void PciModernBackend::DeviceConfigRead(uint16_t offset, uint8_t* value) {
    fbl::AutoLock lock(&lock_);
    MmioRead(reinterpret_cast<volatile uint8_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigRead(uint16_t offset, uint16_t* value) {
    fbl::AutoLock lock(&lock_);
    MmioRead(reinterpret_cast<volatile uint16_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigRead(uint16_t offset, uint32_t* value) {
    fbl::AutoLock lock(&lock_);
    MmioRead(reinterpret_cast<volatile uint32_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigRead(uint16_t offset, uint64_t* value) {
    fbl::AutoLock lock(&lock_);
    MmioRead(reinterpret_cast<volatile uint64_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint8_t value) {
    fbl::AutoLock lock(&lock_);
    MmioWrite(reinterpret_cast<volatile uint8_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint16_t value) {
    fbl::AutoLock lock(&lock_);
    MmioWrite(reinterpret_cast<volatile uint16_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint32_t value) {
    fbl::AutoLock lock(&lock_);
    MmioWrite(reinterpret_cast<volatile uint32_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint64_t value) {
    fbl::AutoLock lock(&lock_);
    MmioWrite(reinterpret_cast<volatile uint64_t*>(device_cfg_ + offset), value);
}

// Attempt to map a bar found in a capability structure. If it has already been
// mapped and we have stored a valid handle in the structure then just return
// ZX_OK.
zx_status_t PciModernBackend::MapBar(uint8_t bar) {
    if (bar >= countof(bar_)) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (bar_[bar].mmio_handle != ZX_HANDLE_INVALID) {
        return ZX_OK;
    }

    size_t size;
    zx_handle_t handle;
    void* base;
    zx_status_t s = pci_map_bar(&pci_, bar, ZX_CACHE_POLICY_UNCACHED_DEVICE, &base, &size, &handle);
    if (s != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to map bar %u: %d\n", tag(), bar, s);
        return s;
    }

    // Store the base as a uintptr_t due to the amount of math done on it later
    bar_[bar].mmio_base = reinterpret_cast<uintptr_t>(base);
    bar_[bar].mmio_handle.reset(handle);
    zxlogf(TRACE, "%s: bar %u mapped to %#" PRIxPTR "\n", tag(), bar, bar_[bar].mmio_base);
    return ZX_OK;
}

void PciModernBackend::CommonCfgCallbackLocked(const virtio_pci_cap_t& cap) {
    zxlogf(TRACE, "%s: common cfg found in bar %u offset %#x\n", tag(), cap.bar, cap.offset);
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    // Common config is a structure of type virtio_pci_common_cfg_t located at an
    // the bar and offset specified by the capability.
    auto addr = bar_[cap.bar].mmio_base + cap.offset;
    common_cfg_ = reinterpret_cast<volatile virtio_pci_common_cfg_t*>(addr);

    // Cache this when we find the config for kicking the queues later
}

void PciModernBackend::NotifyCfgCallbackLocked(const virtio_pci_cap_t& cap) {
    zxlogf(TRACE, "%s: notify cfg found in bar %u offset %#x\n", tag(), cap.bar, cap.offset);
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    notify_base_ = bar_[cap.bar].mmio_base + cap.offset;
}

void PciModernBackend::IsrCfgCallbackLocked(const virtio_pci_cap_t& cap) {
    zxlogf(TRACE, "%s: isr cfg found in bar %u offset %#x\n", tag(), cap.bar, cap.offset);
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    // interrupt status is directly read from the register at this address
    isr_status_ = reinterpret_cast<volatile uint32_t*>(bar_[cap.bar].mmio_base + cap.offset);
}

void PciModernBackend::DeviceCfgCallbackLocked(const virtio_pci_cap_t& cap) {
    zxlogf(TRACE, "%s: device cfg found in bar %u offset %#x\n", tag(), cap.bar, cap.offset);
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    device_cfg_ = bar_[cap.bar].mmio_base + cap.offset;
}

void PciModernBackend::PciCfgCallbackLocked(const virtio_pci_cap_t& cap) {
    // We are not using this capability presently since we can map the
    // bars for direct memory access.
}

// Get the ring size of a specific index
uint16_t PciModernBackend::GetRingSize(uint16_t index) {
    fbl::AutoLock lock(&lock_);

    uint16_t queue_size = 0;
    MmioWrite(&common_cfg_->queue_select, index);
    MmioRead(&common_cfg_->queue_size, &queue_size);
    return queue_size;
}

// Set up ring descriptors with the backend.
void PciModernBackend::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail, zx_paddr_t pa_used) {
    fbl::AutoLock lock(&lock_);

    // These offsets are wrong and this should be changed
    MmioWrite(&common_cfg_->queue_select, index);
    MmioWrite(&common_cfg_->queue_size, count);
    MmioWrite(&common_cfg_->queue_desc, pa_desc);
    MmioWrite(&common_cfg_->queue_avail, pa_avail);
    MmioWrite(&common_cfg_->queue_used, pa_used);
    MmioWrite<uint16_t>(&common_cfg_->queue_enable, 1);

    // Assert that queue_notify_off is equal to the ring index.
    uint16_t queue_notify_off;
    MmioRead(&common_cfg_->queue_notify_off, &queue_notify_off);
    ZX_ASSERT(queue_notify_off == index);
}

void PciModernBackend::RingKick(uint16_t ring_index) {
    fbl::AutoLock lock(&lock_);

    // Virtio 1.0 Section 4.1.4.4
    // The address to notify for a queue is calculated using information from
    // the notify_off_multiplier, the capability's base + offset, and the
    // selected queue's offset.
    //
    // For performance reasons, we assume that the selected queue's offset is
    // equal to the ring index.
    auto addr = notify_base_ + ring_index * notify_off_mul_;
    auto ptr = reinterpret_cast<volatile uint16_t*>(addr);
    zxlogf(SPEW, "%s: kick %u addr %p\n", tag(), ring_index, ptr);
    *ptr = ring_index;
}

bool PciModernBackend::ReadFeature(uint32_t feature) {
    fbl::AutoLock lock(&lock_);
    uint32_t select = feature / 32;
    uint32_t bit = feature % 32;
    uint32_t val;

    MmioWrite(&common_cfg_->device_feature_select, select);
    MmioRead(&common_cfg_->device_feature, &val);
    bool is_set = (val & (1u << bit)) != 0;
    zxlogf(TRACE, "%s: read feature bit %u = %u\n", tag(), feature, is_set);
    return is_set;
}

void PciModernBackend::SetFeature(uint32_t feature) {
    fbl::AutoLock lock(&lock_);
    uint32_t select = feature / 32;
    uint32_t bit = feature % 32;
    uint32_t val;

    MmioWrite(&common_cfg_->driver_feature_select, select);
    MmioRead(&common_cfg_->driver_feature, &val);
    MmioWrite(&common_cfg_->driver_feature, val | (1u << bit));
    zxlogf(TRACE, "%s: feature bit %u now set\n", tag(), feature);
}

zx_status_t PciModernBackend::ConfirmFeatures() {
    fbl::AutoLock lock(&lock_);
    uint8_t val;

    MmioRead(&common_cfg_->device_status, &val);
    val |= VIRTIO_STATUS_FEATURES_OK;
    MmioWrite(&common_cfg_->device_status, val);

    // Check that the device confirmed our feature choices were valid
    MmioRead(&common_cfg_->device_status, &val);
    if ((val & VIRTIO_STATUS_FEATURES_OK) == 0) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_OK;
}

void PciModernBackend::DeviceReset() {
    fbl::AutoLock lock(&lock_);

    MmioWrite<uint8_t>(&common_cfg_->device_status, 0u);
}

void PciModernBackend::DriverStatusOk() {
    fbl::AutoLock lock(&lock_);

    uint8_t device_status;
    MmioRead(&common_cfg_->device_status, &device_status);
    device_status |= VIRTIO_STATUS_DRIVER_OK;
    MmioWrite(&common_cfg_->device_status, device_status);
}

void PciModernBackend::DriverStatusAck() {
    fbl::AutoLock lock(&lock_);

    uint8_t device_status;
    MmioRead(&common_cfg_->device_status, &device_status);
    device_status |= VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    MmioWrite(&common_cfg_->device_status, device_status);
}

uint32_t PciModernBackend::IsrStatus() {
    return (*isr_status_ & (VIRTIO_ISR_QUEUE_INT | VIRTIO_ISR_DEV_CFG_INT));
}

} // namespace virtio
