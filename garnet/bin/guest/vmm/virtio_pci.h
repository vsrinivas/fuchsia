// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_VIRTIO_PCI_H_
#define GARNET_BIN_GUEST_VMM_VIRTIO_PCI_H_

#include <lib/fit/function.h>
#include <virtio/virtio.h>
#include <zircon/types.h>

#include "garnet/bin/guest/vmm/pci.h"

// Virtio PCI Bar Layout.
//
// Expose all read/write fields on BAR0 using a strongly ordered mapping.
// Map the Queue notify region to BAR1 with a BELL type that does not require
// the guest to decode any instruction fields. The queue to notify can be
// inferred based on the address accessed alone.
//
//          BAR0                BAR1
//      ------------  00h   ------------  00h
//     | Virtio PCI |      |  Queue 0   |
//     |   Common   |      |   Notify   |
//     |   Config   |      |------------| 04h
//     |------------| 38h  |  Queue 1   |
//     | ISR Config |      |   Notify   |
//     |------------| 3ch  |------------|
//     |  Device-   |      |    ...     |
//     | Specific   |      |------------| 04 * N
//     |  Config    |      |  Queue N   |
//     |            |      |   Notify   |
//      ------------        ------------
// These structures are defined in Virtio 1.0 Section 4.1.4.
static constexpr uint8_t kVirtioPciBar = 0;
static constexpr uint8_t kVirtioPciNotifyBar = 1;
static_assert(kVirtioPciBar < kPciMaxBars && kVirtioPciNotifyBar < kPciMaxBars,
              "Not enough BAR registers available");

static constexpr size_t kVirtioPciNumCapabilities = 4;

// We initialize Virtio devices with a ring size so that a sensible size is set,
// even if they do not configure one themselves.
static constexpr uint16_t kDefaultVirtioQueueSize = 128;

// Queue addresses as defined in Virtio 1.0 Section 4.1.4.3.
struct VirtioQueueConfig {
  union {
    struct {
      uint64_t desc;
      uint64_t avail;
      uint64_t used;
    };

    // Software will access these using 32 bit operations. Provide a
    // convenience interface for these use cases.
    uint32_t words[6] = {};
  };

  uint16_t size = kDefaultVirtioQueueSize;
};

struct VirtioDeviceConfig {
  mutable std::mutex mutex;

  // Virtio device ID.
  const uint16_t device_id = 0;

  // Virtio device features.
  const uint32_t device_features = 0;

  // Pointer to device configuration.
  void* config __TA_GUARDED(mutex) = nullptr;

  // Number of bytes used for this device's configuration space.
  //
  // This should cover only bytes used for the device-specific portions of
  // the configuration header, omitting any of the (transport-specific)
  // shared configuration space.
  const uint64_t config_size = 0;

  // Virtio queues for this device.
  VirtioQueueConfig* const queue_configs __TA_GUARDED(mutex) = nullptr;

  // Number of Virtio queues.
  const uint16_t num_queues = 0;

  // Invoked when the driver has made a change to the queue configuration.
  using ConfigQueueFn =
      fit::function<zx_status_t(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                zx_gpaddr_t avail, zx_gpaddr_t used)>;
  const ConfigQueueFn config_queue;

  // Invoked when the driver sends notifications on a queue to the device.
  //
  // TODO(abdulla): Remove this once all devices are out-of-process.
  using NotifyQueueFn = fit::function<zx_status_t(uint16_t queue)>;
  const NotifyQueueFn notify_queue;

  // Invoked when the driver has made a change to the device configuration.
  using ConfigDeviceFn =
      fit::function<zx_status_t(uint64_t addr, const IoValue& value)>;
  const ConfigDeviceFn config_device;

  // Invoked when the driver has accepted features and set the device into a
  // 'Ready' state.
  //
  // Devices can place logic here that depends on the set of negotiated
  // features with the driver.
  using ReadyDeviceFn =
      fit::function<zx_status_t(uint32_t negotiated_features)>;
  const ReadyDeviceFn ready_device;
};

// Virtio PCI transport implementation.
class VirtioPci : public PciDevice {
 public:
  explicit VirtioPci(VirtioDeviceConfig* device_config);

  // Read a value at |bar| and |offset| from this device.
  zx_status_t ReadBar(uint8_t bar, uint64_t offset,
                      IoValue* value) const override;
  // Write a value at |bar| and |offset| to this device.
  zx_status_t WriteBar(uint8_t bar, uint64_t offset,
                       const IoValue& value) override;

  // ISR flag values.
  enum IsrFlags : uint8_t {
    // Interrupt is caused by a queue.
    ISR_QUEUE = 1 << 0,
    // Interrupt is caused by a device config change.
    ISR_CONFIG = 1 << 1,
  };
  // Sets the given flags in the ISR register.
  void add_isr_flags(uint8_t flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    isr_status_ |= flags;
  }

  // Device features.
  //
  // These are feature bits that are supported by the device. They may or
  // may not correspond to the set of feature flags that have been negotiated
  // at runtime. For negotiated features, see |has_negotiated_features|.
  bool has_device_features(uint32_t features) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (device_config_->device_features & features) == features;
  }

  // Returns true if the set of features have been negotiated to be enabled.
  bool has_negotiated_features(uint32_t features) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (device_config_->device_features & driver_features_ & features) ==
           features;
  }

 private:
  bool HasPendingInterrupt() const override;

  // Handle accesses to the general configuration BAR.
  zx_status_t ConfigBarRead(uint64_t addr, IoValue* value) const;
  zx_status_t ConfigBarWrite(uint64_t addr, const IoValue& value);

  // Handle accesses to the common configuration region.
  zx_status_t CommonCfgRead(uint64_t addr, IoValue* value) const;
  zx_status_t CommonCfgWrite(uint64_t addr, const IoValue& value);

  // Handle writes to the notify BAR.
  zx_status_t NotifyBarWrite(uint64_t addr, const IoValue& value);

  void SetupCaps();

  uint16_t queue_sel() const;

  VirtioDeviceConfig* const device_config_;

  // We need one of these for every virtio_pci_cap_t structure we expose.
  pci_cap_t capabilities_[kVirtioPciNumCapabilities];
  // Virtio PCI capabilities.
  virtio_pci_cap_t common_cfg_cap_;
  virtio_pci_cap_t device_cfg_cap_;
  virtio_pci_notify_cap_t notify_cfg_cap_;
  virtio_pci_cap_t isr_cfg_cap_;

  mutable std::mutex mutex_;

  // Device feature bits.
  //
  // Defined in Virtio 1.0 Section 2.2.
  uint32_t device_features_sel_ __TA_GUARDED(mutex_) = 0;

  // Driver feature bits.
  uint32_t driver_features_ __TA_GUARDED(mutex_) = 0;
  uint32_t driver_features_sel_ __TA_GUARDED(mutex_) = 0;

  // Device status field as defined in Virtio 1.0, Section 2.1.
  uint8_t status_ __TA_GUARDED(mutex_) = 0;

  // Interrupt status register.
  mutable uint8_t isr_status_ __TA_GUARDED(mutex_) = 0;

  // Index of the queue currently selected by the driver.
  uint16_t queue_sel_ __TA_GUARDED(mutex_) = 0;
};

#endif  // GARNET_BIN_GUEST_VMM_VIRTIO_PCI_H_
