// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
#define GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <virtio/virtio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_pci.h"
#include "garnet/lib/machina/virtio_queue.h"

namespace machina {

class VirtioDevice;

// Base class for all virtio devices.
class VirtioDevice {
 public:
  virtual ~VirtioDevice() = default;

  // Read a device-specific configuration field.
  virtual zx_status_t ReadConfig(uint64_t addr, IoValue* value);

  // Write a device-specific configuration field.
  virtual zx_status_t WriteConfig(uint64_t addr, const IoValue& value);

  // Handle notify events for one of this devices queues.
  virtual zx_status_t HandleQueueNotify(uint16_t queue_sel) { return ZX_OK; }

  // Send a notification back to the guest that there are new descriptors in
  // then used ring.
  //
  // The method for how this notification is delievered is transport
  // specific.
  zx_status_t NotifyGuest();

  const PhysMem& phys_mem() { return phys_mem_; }
  uint16_t num_queues() const { return num_queues_; }

  // ISR flag values.
  enum IsrFlags : uint8_t {
    // Interrupt is caused by a queue.
    VIRTIO_ISR_QUEUE = 0x1,
    // Interrupt is caused by a device config change.
    VIRTIO_ISR_DEVICE = 0x2,
  };

  // Sets the given flags in the ISR register.
  void add_isr_flags(uint8_t flags) {
    fbl::AutoLock lock(&mutex_);
    isr_status_ |= flags;
  }

  // Device features.
  //
  // These are feature bits that are supported by the device. They may or
  // may not correspond to the set of feature flags that have been negotiated
  // at runtime.
  void add_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    features_ |= features;
  }
  bool has_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & features) == features;
  }

  PciDevice* pci_device() { return &pci_; }

 protected:
  VirtioDevice(uint8_t device_id,
               void* config,
               size_t config_size,
               virtio_queue_t* queues,
               uint16_t num_queues,
               const PhysMem& phys_mem);

  // Mutex for accessing device configuration fields.
  fbl::Mutex config_mutex_;

 private:
  // Temporarily expose our state to the PCI transport until the proper
  // accessor methods are defined.
  friend class VirtioPci;

  fbl::Mutex mutex_;

  // Handle kicks from the driver that a queue needs attention.
  zx_status_t Kick(uint16_t queue_sel);

  // Device feature bits.
  //
  // Defined in Virtio 1.0 Section 2.2.
  uint32_t features_ __TA_GUARDED(mutex_) = 0;
  uint32_t features_sel_ __TA_GUARDED(mutex_) = 0;

  // Driver feature bits.
  uint32_t driver_features_ __TA_GUARDED(mutex_) = 0;
  uint32_t driver_features_sel_ __TA_GUARDED(mutex_) = 0;

  // Virtio device id.
  const uint8_t device_id_;

  // Device status field as defined in Virtio 1.0, Section 2.1.
  uint8_t status_ __TA_GUARDED(mutex_) = 0;

  // Interrupt status register.
  uint8_t isr_status_ __TA_GUARDED(mutex_) = 0;

  // Index of the queue currently selected by the driver.
  uint16_t queue_sel_ __TA_GUARDED(mutex_) = 0;

  // Pointer to the structure that holds this devices configuration
  // structure.
  void* const device_config_ __TA_GUARDED(config_mutex_) = nullptr;

  // Number of bytes used for this devices configuration space.
  //
  // This should cover only bytes used for the device-specific portions of
  // the configuration header, omitting any of the (transport-specific)
  // shared configuration space.
  const size_t device_config_size_ = 0;

  // Virtqueues for this device.
  virtio_queue_t* const queues_ = nullptr;

  // Size of queues array.
  const uint16_t num_queues_ = 0;

  // Guest physical memory.
  const PhysMem& phys_mem_;

  // Virtio PCI transport.
  VirtioPci pci_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
