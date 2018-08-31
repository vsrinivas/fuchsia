// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
#define GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_

#include <atomic>
#include <mutex>

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

// Interface for all virtio devices.
template <uint8_t DeviceId, uint16_t NumQueues, typename ConfigType>
class VirtioDevice {
 public:
  PciDevice* pci_device() { return &pci_; }

 protected:
  const PhysMem& phys_mem_;
  ConfigType config_ __TA_GUARDED(device_config_.mutex) = {};
  VirtioDeviceConfig device_config_;
  VirtioPci pci_;
  VirtioQueue queues_[NumQueues];

  VirtioDevice(const PhysMem& phys_mem, uint32_t device_features,
               VirtioDeviceConfig::NotifyQueueFn notify_queue,
               VirtioDeviceConfig::UpdateConfigFn update_config)
      : phys_mem_(phys_mem),
        device_config_{
            .device_id = DeviceId,
            // Advertise support for common/bus features.
            .device_features = device_features | kVirtioBusFeatures,
            .config = &config_,
            .config_size = sizeof(ConfigType),
            .queues = queues_,
            .num_queues = NumQueues,
            .notify_queue = std::move(notify_queue),
            .update_config = std::move(update_config),
            // TODO(abdulla): Use this in a multi-process VMM.
            .ready_device = [](uint32_t) { return ZX_OK; },
        },
        pci_(&device_config_) {
    for (int i = 0; i < NumQueues; ++i) {
      queues_[i].set_phys_mem(&phys_mem);
      queues_[i].set_interrupt(
          fit::bind_member(this, &VirtioDevice::Interrupt));
    }
  }

  VirtioDevice(const PhysMem& phys_mem, uint32_t device_features,
               VirtioDeviceConfig::NotifyQueueFn notify_queue)
      : VirtioDevice(phys_mem, device_features, std::move(notify_queue),
                     [](uint64_t, const IoValue&) { return ZX_OK; }) {}

  VirtioDevice(const PhysMem& phys_mem, uint32_t device_features)
      : VirtioDevice(phys_mem, device_features,
                     fit::bind_member(this, &VirtioDevice::NotifyQueue)) {}

  virtual ~VirtioDevice() = default;

  // Sets interrupt flag, and possibly sends interrupt to the driver.
  zx_status_t Interrupt(VirtioQueue::InterruptAction action =
                            VirtioQueue::InterruptAction::SEND_INTERRUPT) {
    // Set the queue bit in the device ISR so that the driver knows to check
    // the queues on the next interrupt.
    pci_.add_isr_flags(VirtioPci::VIRTIO_ISR_QUEUE);
    if (action == VirtioQueue::InterruptAction::SEND_INTERRUPT) {
      return pci_.Interrupt();
    }
    return ZX_OK;
  }

  // Processes notifications on a queue from the driver.
  //
  // TODO(abdulla): Remove this once all devices are out-of-process.
  zx_status_t NotifyQueue(uint16_t queue) {
    if (queue >= NumQueues) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    // Generate a flow ID that will be later read by the queue request handler
    // to trace correlation from notifications generated from PCI bus traps /
    // interrupts to their corresponding descriptor processing in the queue
    // handler. As there is no exact mapping between notifications and
    // descriptors in the queue, correlation tracing should only be considered
    // best-effort and may provide inaccurate correlations if new notifications
    // happen while the queue is not empty.
    const trace_async_id_t flow_id =
        (static_cast<trace_async_id_t>(DeviceId) << 56) +
        (static_cast<trace_async_id_t>(queue) << 40) + TRACE_NONCE();
    TRACE_DURATION("machina", "queue_notify", "device_id", DeviceId, "queue",
                   queue, "flow_id", flow_id);

    // Only emplace a new flow ID if there is no other still in flight.
    trace_async_id_t unset = 0;
    if (trace_flow_id(queue)->compare_exchange_strong(unset, flow_id)) {
      TRACE_FLOW_BEGIN("machina", "queue_signal", flow_id);
    }

    // Send an interrupt back to the guest if we've generated one while
    // processing the queue.
    zx_status_t status = pci_.Interrupt();
    if (status != ZX_OK) {
      return status;
    }

    // Notify threads waiting on a descriptor.
    return queues_[queue].Notify();
  }

  VirtioQueue* queue(uint16_t sel) {
    return sel >= NumQueues ? nullptr : &queues_[sel];
  }

  std::atomic<trace_async_id_t>* trace_flow_id(uint16_t sel) {
    return sel >= NumQueues ? nullptr : &trace_flow_ids_[sel];
  }

 private:
  // One flow ID slot for each device queue, used for IO correlation tracing.
  std::atomic<trace_async_id_t> trace_flow_ids_[NumQueues] = {};
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
