// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
#define GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_

#include <atomic>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <trace-engine/types.h>
#include <trace/event.h>
#include <virtio/virtio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_pci.h"
#include "garnet/lib/machina/virtio_queue.h"

namespace machina {

// Set of features that are supported by the bus transparently for all devices.
static constexpr uint32_t kVirtioBusFeatures = 0;

class VirtioDevice;

// Interface for all virtio devices.
class VirtioDevice {
 public:
  virtual ~VirtioDevice();

  // Read a device-specific configuration field.
  virtual zx_status_t ReadConfig(uint64_t addr, IoValue* value) = 0;

  // Write a device-specific configuration field.
  virtual zx_status_t WriteConfig(uint64_t addr, const IoValue& value) = 0;

  // Handle notify events for one of this devices queues.
  virtual zx_status_t HandleQueueNotify(uint16_t queue_sel) { return ZX_OK; }

  // Send a notification back to the guest that there are new descriptors in
  // the used ring.
  //
  // The method for how this notification is delivered is transport specific.
  zx_status_t NotifyGuest();

  // Invoked when the driver has accepted features and set the device into a
  // 'Ready' state.
  //
  // Devices can place logic here that depends on the set of negotiated
  // features with the driver.
  virtual void OnDeviceReady() {}

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
  // at runtime. For negotiated features, see |has_enabled_features|.
  void add_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    features_ |= features;
  }
  bool has_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & features) == features;
  }

  // Returns true if the set of features have been negotiated to be enabled.
  bool has_enabled_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & driver_features_ & features) == features;
  }

  PciDevice* pci_device() { return &pci_; }

 protected:
  VirtioDevice(uint8_t device_id, size_t config_size, VirtioQueue* queues,
               uint16_t num_queues, const PhysMem& phys_mem);

  // Handles kicks from the driver that a queue needs attention.
  virtual zx_status_t Kick(uint16_t kicked_queue);

 private:
  // Temporarily expose our state to the PCI transport until the proper
  // accessor methods are defined.
  friend class VirtioPci;

  fbl::Mutex mutex_;

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

  // Number of bytes used for this devices configuration space.
  //
  // This should cover only bytes used for the device-specific portions of
  // the configuration header, omitting any of the (transport-specific)
  // shared configuration space.
  const size_t device_config_size_ = 0;

  // Virtio queues for this device.
  VirtioQueue* const queues_ = nullptr;

  // Size of queues array.
  const uint16_t num_queues_ = 0;

  // Guest physical memory.
  const PhysMem& phys_mem_;

  // Virtio PCI transport.
  VirtioPci pci_;
};

template <uint16_t VIRTIO_ID, int NUM_QUEUES, typename ConfigType,
          uint16_t QUEUE_SIZE_MAX = 128>
class VirtioDeviceBase : public VirtioDevice {
 public:
  VirtioDeviceBase(const PhysMem& phys_mem)
      : VirtioDevice(VIRTIO_ID, sizeof(config_), queues_, NUM_QUEUES,
                     phys_mem) {
    // Advertise support for common/bus features.
    add_device_features(kVirtioBusFeatures);
    for (int i = 0; i < NUM_QUEUES; ++i) {
      queues_[i].set_size(QUEUE_SIZE_MAX);
      queues_[i].set_device(this);
    }
  }

  zx_status_t ReadConfig(uint64_t addr, IoValue* value) override {
    fbl::AutoLock lock(&config_mutex_);
    switch (value->access_size) {
      case 1: {
        uint8_t* buf = reinterpret_cast<uint8_t*>(&config_);
        value->u8 = buf[addr];
        return ZX_OK;
      }
      case 2: {
        uint16_t* buf = reinterpret_cast<uint16_t*>(&config_);
        value->u16 = buf[addr / 2];
        return ZX_OK;
      }
      case 4: {
        uint32_t* buf = reinterpret_cast<uint32_t*>(&config_);
        value->u32 = buf[addr / 4];
        return ZX_OK;
      }
    }
    FXL_LOG(ERROR) << "Unsupported config read 0x" << std::hex << addr;
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override {
    fbl::AutoLock lock(&config_mutex_);
    switch (value.access_size) {
      case 1: {
        uint8_t* buf = reinterpret_cast<uint8_t*>(&config_);
        buf[addr] = value.u8;
        return ZX_OK;
      }
      case 2: {
        uint16_t* buf = reinterpret_cast<uint16_t*>(&config_);
        buf[addr / 2] = value.u16;
        return ZX_OK;
      }
      case 4: {
        uint32_t* buf = reinterpret_cast<uint32_t*>(&config_);
        buf[addr / 4] = value.u32;
        return ZX_OK;
      }
    }
    FXL_LOG(ERROR) << "Unsupported config write 0x" << std::hex << addr;
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t Kick(uint16_t kicked_queue) override {
    if (kicked_queue >= NUM_QUEUES) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    // Generate a flow ID that will be later read by the queue request handler
    // to trace correlation from kicks generated from PCI bus traps/interrupts
    // to their corresponding descriptor processing in the queue handler. As
    // there is no exact mapping between kicks and descriptors in the queue,
    // correlation tracing should only be considered best-effort and may provide
    // inaccurate correlations if new kicks happen while the queue is not empty.
    const trace_async_id_t flow_id =
        (static_cast<trace_async_id_t>(VIRTIO_ID) << 56) +
        (static_cast<trace_async_id_t>(kicked_queue) << 40) + TRACE_NONCE();
    TRACE_DURATION("machina", "io_queue_kick", "device_id", VIRTIO_ID,
                   "kicked_queue", kicked_queue, "flow_id", flow_id);

    // Only emplace a new flow ID if there is no other still in flight.
    trace_async_id_t unset = 0;
    if (trace_flow_id(kicked_queue)->compare_exchange_strong(unset, flow_id)) {
      TRACE_FLOW_BEGIN("machina", "io_queue_signal", flow_id);
    }

    // Perform the actual kick.
    return VirtioDevice::Kick(kicked_queue);
  }

  VirtioQueue* queue(uint16_t sel) {
    return sel >= NUM_QUEUES ? nullptr : &queues_[sel];
  }

  std::atomic<trace_async_id_t>* trace_flow_id(uint16_t sel) {
    return sel >= NUM_QUEUES ? nullptr : &trace_flow_ids_[sel];
  }

 protected:
  // Mutex for accessing device configuration fields.
  mutable fbl::Mutex config_mutex_;
  ConfigType config_ __TA_GUARDED(config_mutex_) = {};

 private:
  VirtioQueue queues_[NUM_QUEUES];

  // One flow ID slot for each device queue, used for IO correlation tracing.
  std::atomic<trace_async_id_t> trace_flow_ids_[NUM_QUEUES] = {};
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
