// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_LIB_VIRTIO_INCLUDE_LIB_VIRTIO_BACKENDS_PCI_H_
#define SRC_DEVICES_BUS_LIB_VIRTIO_INCLUDE_LIB_VIRTIO_BACKENDS_PCI_H_

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/virtio/backends/backend.h>
#include <lib/zx/port.h>
#include <zircon/compiler.h>

#include <optional>

namespace virtio {

class PciBackend : public Backend {
 public:
  PciBackend(ddk::PciProtocolClient pci, pcie_device_info_t info);
  zx_status_t Bind() final;
  virtual zx_status_t Init() = 0;
  const char* tag() { return tag_; }

  zx_status_t ConfigureIrqMode();
  zx::status<uint32_t> WaitForInterrupt() final;
  void InterruptAck(uint32_t key) final;

  // Virtio spec 4.1.5.1.2 - MSI-X Vector Configuration
  static constexpr uint16_t kVirtioMsiNoVector = 0xFFFF;
  static constexpr uint16_t kMsiConfigVector = 0;
  static constexpr uint16_t kMsiQueueVector = 1;

 protected:
  const ddk::PciProtocolClient& pci() { return pci_; }
  pcie_device_info_t info() { return info_; }
  fbl::Mutex& lock() { return lock_; }
  zx::port& wait_port() { return wait_port_; }

 private:
  ddk::PciProtocolClient pci_;
  pcie_device_info_t info_;
  fbl::Mutex lock_;
  zx::port wait_port_;
  char tag_[16];  // pci[XX:XX.X] + \0, aligned to 8
  DISALLOW_COPY_ASSIGN_AND_MOVE(PciBackend);
};

// PciLegacyBackend corresponds to the Virtio Legacy interface utilizing port IO and
// the IO Bar 0. It has complications with address offsets when MSI-X is enabled.
class PciLegacyBackend : public PciBackend {
 public:
  PciLegacyBackend(ddk::PciProtocolClient pci, pcie_device_info_t info) : PciBackend(pci, info) {}
  ~PciLegacyBackend() override;
  zx_status_t Init() final;

  void DriverStatusOk() final;
  void DriverStatusAck() final;
  void DeviceReset() final;
  uint32_t IsrStatus() final;
  bool ReadFeature(uint32_t feature) final;
  void SetFeature(uint32_t feature) final;
  zx_status_t ConfirmFeatures() final;

  // These handle reading and writing a device's device config to allow derived
  // virtio devices to work with fields only they know about. For most virtio
  // devices they will have their device config copied over via
  // CopyDeviceConfig when device config interrupts are asserted and will not
  // need to call these directly.
  void ReadDeviceConfig(uint16_t offset, uint8_t* value) final __TA_EXCLUDES(lock());
  void ReadDeviceConfig(uint16_t offset, uint16_t* value) final __TA_EXCLUDES(lock());
  void ReadDeviceConfig(uint16_t offset, uint32_t* value) final __TA_EXCLUDES(lock());
  void ReadDeviceConfig(uint16_t offset, uint64_t* value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint8_t value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint16_t value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint32_t value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint64_t value) final __TA_EXCLUDES(lock());

  // Handle the virtio queues for the device. Due to configuration layouts changing
  // depending on backend this has to be handled by the backend itself.
  uint16_t GetRingSize(uint16_t index) final;
  void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
               zx_paddr_t pa_used) final;
  void RingKick(uint16_t ring_index) final;

 private:
  void IoReadLocked(uint16_t offset, uint8_t* val) __TA_REQUIRES(lock());
  void IoReadLocked(uint16_t offset, uint16_t* val) __TA_REQUIRES(lock());
  void IoReadLocked(uint16_t offset, uint32_t* val) __TA_REQUIRES(lock());
  void IoWriteLocked(uint16_t offset, uint8_t val) __TA_REQUIRES(lock());
  void IoWriteLocked(uint16_t offset, uint16_t val) __TA_REQUIRES(lock());
  void IoWriteLocked(uint16_t offset, uint32_t val) __TA_REQUIRES(lock());
  void SetStatusBits(uint8_t bits);
  uint16_t bar0_base_ __TA_GUARDED(lock());
  uint16_t device_cfg_offset_ __TA_GUARDED(lock());

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(PciLegacyBackend);
};

// PciModernBackend is for v1.0+ Virtio using MMIO mapped bars and PCI capabilities.
class PciModernBackend : public PciBackend {
 public:
  PciModernBackend(ddk::PciProtocolClient pci, pcie_device_info_t info) : PciBackend(pci, info) {}
  // The dtor handles cleanup of allocated bars because we cannot tear down
  // the mappings safely while the virtio device is being used by a driver.
  ~PciModernBackend() override = default;
  zx_status_t Init() final;

  void DriverStatusOk() final;
  void DriverStatusAck() final;
  void DeviceReset() final;
  uint32_t IsrStatus() final;
  bool ReadFeature(uint32_t feature) final;
  void SetFeature(uint32_t feature) final;
  zx_status_t ConfirmFeatures() final;
  zx_status_t ReadVirtioCap(uint8_t offset, virtio_pci_cap* cap);

  // These handle writing to/from a device's device config to allow derived
  // virtio devices to work with fields only they know about.
  void ReadDeviceConfig(uint16_t offset, uint8_t* value) final __TA_EXCLUDES(lock());
  void ReadDeviceConfig(uint16_t offset, uint16_t* value) final __TA_EXCLUDES(lock());
  void ReadDeviceConfig(uint16_t offset, uint32_t* value) final __TA_EXCLUDES(lock());
  void ReadDeviceConfig(uint16_t offset, uint64_t* value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint8_t value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint16_t value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint32_t value) final __TA_EXCLUDES(lock());
  void WriteDeviceConfig(uint16_t offset, uint64_t value) final __TA_EXCLUDES(lock());

  // Callbacks called during PciBackend's parsing of capabilities in Bind()
  void CommonCfgCallbackLocked(const virtio_pci_cap_t& cap) __TA_REQUIRES(lock());
  void NotifyCfgCallbackLocked(const virtio_pci_cap_t& cap) __TA_REQUIRES(lock());
  void IsrCfgCallbackLocked(const virtio_pci_cap_t& cap) __TA_REQUIRES(lock());
  void DeviceCfgCallbackLocked(const virtio_pci_cap_t& cap) __TA_REQUIRES(lock());
  void PciCfgCallbackLocked(const virtio_pci_cap_t& cap) __TA_REQUIRES(lock());

  // Handle the virtio queues for the device. Due to configuration layouts changing
  // depending on backend this has to be handled by the backend itself.
  uint16_t GetRingSize(uint16_t index) final;
  void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
               zx_paddr_t pa_used) final;
  void RingKick(uint16_t ring_index) final;

 private:
  zx_status_t MapBar(uint8_t bar);

  std::optional<ddk::MmioBuffer> bar_[6];

  uintptr_t notify_base_ = 0;
  volatile uint32_t* isr_status_ = nullptr;
  uintptr_t device_cfg_ __TA_GUARDED(lock()) = 0;
  volatile virtio_pci_common_cfg_t* common_cfg_ __TA_GUARDED(lock()) = nullptr;
  uint32_t notify_off_mul_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(PciModernBackend);
};

}  // namespace virtio

#endif  // SRC_DEVICES_BUS_LIB_VIRTIO_INCLUDE_LIB_VIRTIO_BACKENDS_PCI_H_
