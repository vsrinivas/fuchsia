// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "backend.h"
#include <zircon/thread_annotations.h>

namespace virtio {

class PciBackend : public Backend {
public:
    PciBackend(pci_protocol_t pci, zx_pcie_device_info_t info);
    zx_status_t Bind() override;
    virtual zx_status_t Init() = 0;
    const char* tag() { return tag_; }

    zx_status_t InterruptValid() override;
    zx_status_t WaitForInterrupt() override;

protected:
    pci_protocol_t pci_ = {nullptr, nullptr};
    zx_pcie_device_info_t info_;
    fbl::Mutex lock_;
    char tag_[16]; // pci[XX:XX.X] + \0, aligned to 8

    DISALLOW_COPY_ASSIGN_AND_MOVE(PciBackend);
};

class PciLegacyBackend : public PciBackend {
public:
    PciLegacyBackend(pci_protocol_t pci, zx_pcie_device_info_t info)
        : PciBackend(pci, info) {}
    virtual ~PciLegacyBackend();
    zx_status_t Init() override;

    void DriverStatusOk() override;
    void DriverStatusAck() override;
    void DeviceReset() override;
    uint32_t IsrStatus() override;
    bool ReadFeature(uint32_t feature) override;
    void SetFeature(uint32_t feature) override;
    zx_status_t ConfirmFeatures() override;

    // These handle reading and writing a device's device config to allow derived
    // virtio devices to work with fields only they know about. For most virtio
    // devices they will have their device config copied over via
    // CopyDeviceConfig when device config interrupts are asserted and will not
    // need to call these directly.
    void DeviceConfigRead(uint16_t offset, uint8_t* value) override;
    void DeviceConfigRead(uint16_t offset, uint16_t* value) override;
    void DeviceConfigRead(uint16_t offset, uint32_t* value) override;
    void DeviceConfigRead(uint16_t offset, uint64_t* value) override;
    void DeviceConfigWrite(uint16_t offset, uint8_t value) override;
    void DeviceConfigWrite(uint16_t offset, uint16_t value) override;
    void DeviceConfigWrite(uint16_t offset, uint32_t value) override;
    void DeviceConfigWrite(uint16_t offset, uint64_t value) override;

    // Handle the virtio queues for the device. Due to configuration layouts changing
    // depending on backend this has to be handled by the backend itself.
    uint16_t GetRingSize(uint16_t index) override;
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                 zx_paddr_t pa_used) override;
    void RingKick(uint16_t ring_index) override;

private:
    void IoReadLocked(uint16_t port, uint8_t* val);
    void IoReadLocked(uint16_t port, uint16_t* val);
    void IoReadLocked(uint16_t port, uint32_t* val);
    void IoWriteLocked(uint16_t port, uint8_t val);
    void IoWriteLocked(uint16_t port, uint16_t val);
    void IoWriteLocked(uint16_t port, uint32_t val);
    void SetStatusBits(uint8_t bits);
    uint16_t bar0_base_ TA_GUARDED(lock_);
    uint16_t device_cfg_offset_ TA_GUARDED(lock_);

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(PciLegacyBackend);
};

class PciModernBackend : public PciBackend {
public:
    PciModernBackend(pci_protocol_t pci, zx_pcie_device_info_t info)
        : PciBackend(pci, info) {}
    // The dtor handles cleanup of allocated bars because we cannot tear down
    // the mappings safely while the virtio device is being used by a driver.
    virtual ~PciModernBackend(){};
    zx_status_t Init() override;

    void DriverStatusOk() override;
    void DriverStatusAck() override;
    void DeviceReset() override;
    uint32_t IsrStatus() override;
    bool ReadFeature(uint32_t feature) override;
    void SetFeature(uint32_t feature) override;
    zx_status_t ConfirmFeatures() override;

    // These handle writing to/from a device's device config to allow derived
    // virtio devices to work with fields only they know about.
    void DeviceConfigRead(uint16_t offset, uint8_t* value) override;
    void DeviceConfigRead(uint16_t offset, uint16_t* value) override;
    void DeviceConfigRead(uint16_t offset, uint32_t* value) override;
    void DeviceConfigRead(uint16_t offset, uint64_t* value) override;
    void DeviceConfigWrite(uint16_t offset, uint8_t value) override;
    void DeviceConfigWrite(uint16_t offset, uint16_t value) override;
    void DeviceConfigWrite(uint16_t offset, uint32_t value) override;
    void DeviceConfigWrite(uint16_t offset, uint64_t value) override;

    // Callbacks called during PciBackend's parsing of capabilities in Bind()
    void CommonCfgCallbackLocked(const virtio_pci_cap_t& cap) TA_REQ(lock_);
    void NotifyCfgCallbackLocked(const virtio_pci_cap_t& cap) TA_REQ(lock_);
    void IsrCfgCallbackLocked(const virtio_pci_cap_t& cap) TA_REQ(lock_);
    void DeviceCfgCallbackLocked(const virtio_pci_cap_t& cap) TA_REQ(lock_);
    void PciCfgCallbackLocked(const virtio_pci_cap_t& cap) TA_REQ(lock_);

    // Handle the virtio queues for the device. Due to configuration layouts changing
    // depending on backend this has to be handled by the backend itself.
    uint16_t GetRingSize(uint16_t index) override;
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                 zx_paddr_t pa_used) override;
    void RingKick(uint16_t ring_index) override;
    char* tag() { return tag_; }

private:
    zx_status_t MapBar(uint8_t bar);

    struct bar {
        uintptr_t mmio_base;
        zx::handle mmio_handle;
    } bar_[6] = {{0, {}}};

    uintptr_t notify_base_ = 0;
    volatile uint32_t* isr_status_ = nullptr;
    uintptr_t device_cfg_ TA_GUARDED(lock_) = 0;
    volatile virtio_pci_common_cfg_t* common_cfg_ TA_GUARDED(lock_) = nullptr;
    uint32_t notify_off_mul_;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(PciModernBackend);
};

} // namespace virtio
