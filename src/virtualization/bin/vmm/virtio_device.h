// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_DEVICE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_DEVICE_H_

#include <fuchsia/virtualization/hardware/cpp/fidl.h>

#include <atomic>

#include <trace-engine/types.h>
#include <trace/event.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/virtualization/bin/vmm/device/config.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"
#include "src/virtualization/bin/vmm/virtio_pci.h"

// Set of features that are supported transparently for all devices.
static constexpr uint32_t kVirtioFeatures = 0;

constexpr zx_status_t noop_config_queue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return ZX_OK;
}
constexpr zx_status_t noop_notify_queue(uint16_t queue) { return ZX_OK; }
constexpr zx_status_t noop_config_device(uint64_t addr, const IoValue& value) { return ZX_OK; }
constexpr zx_status_t noop_ready_device(uint32_t negotiated_features) { return ZX_OK; }

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
  VirtioQueueConfig queue_configs_[NumQueues] __TA_GUARDED(device_config_.mutex) = {};

  VirtioDevice(const PhysMem& phys_mem, uint32_t device_features,
               VirtioDeviceConfig::ConfigQueueFn config_queue,
               VirtioDeviceConfig::NotifyQueueFn notify_queue,
               VirtioDeviceConfig::ConfigDeviceFn config_device,
               VirtioDeviceConfig::ReadyDeviceFn ready_device)
      : phys_mem_(phys_mem),
        device_config_{
            .device_id = DeviceId,
            // Advertise support for common/bus features.
            .device_features = device_features | kVirtioFeatures,
            .config = &config_,
            .config_size = sizeof(ConfigType),
            .queue_configs = queue_configs_,
            .num_queues = NumQueues,
            .config_queue = std::move(config_queue),
            .notify_queue = std::move(notify_queue),
            .config_device = std::move(config_device),
            .ready_device = std::move(ready_device),
        },
        pci_(&device_config_) {}

  virtual ~VirtioDevice() = default;

  // Sets interrupt flag, and possibly sends interrupt to the driver.
  zx_status_t Interrupt(uint8_t actions) {
    if (actions & VirtioQueue::SET_QUEUE) {
      pci_.add_isr_flags(VirtioPci::ISR_QUEUE);
    }
    if (actions & VirtioQueue::SET_CONFIG) {
      pci_.add_isr_flags(VirtioPci::ISR_CONFIG);
    }
    if (actions & VirtioQueue::TRY_INTERRUPT) {
      return pci_.Interrupt();
    }
    return ZX_OK;
  }
};

template <uint8_t DeviceId, uint16_t NumQueues, typename ConfigType>
class VirtioInprocessDevice : public VirtioDevice<DeviceId, NumQueues, ConfigType> {
 protected:
  VirtioInprocessDevice(const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigDeviceFn config_device,
                        VirtioDeviceConfig::ReadyDeviceFn ready_device)
      : VirtioInprocessDevice(phys_mem, device_features,
                              fit::bind_member(this, &VirtioInprocessDevice::ConfigQueue),
                              fit::bind_member(this, &VirtioInprocessDevice::NotifyQueue),
                              std::move(config_device), std::move(ready_device)) {}

  VirtioInprocessDevice(const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigDeviceFn config_device)
      : VirtioInprocessDevice(phys_mem, device_features, std::move(config_device),
                              noop_ready_device) {}

  VirtioInprocessDevice(const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::NotifyQueueFn notify_queue)
      : VirtioInprocessDevice(phys_mem, device_features,
                              fit::bind_member(this, &VirtioInprocessDevice::ConfigQueue),
                              std::move(notify_queue), noop_config_device, noop_ready_device) {}

  VirtioInprocessDevice(const PhysMem& phys_mem, uint32_t device_features)
      : VirtioInprocessDevice(phys_mem, device_features, noop_config_device) {}

  // Processes notifications on a queue from the driver.
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
    const trace_async_id_t flow_id = (static_cast<trace_async_id_t>(DeviceId) << 56) +
                                     (static_cast<trace_async_id_t>(queue) << 40) + TRACE_NONCE();
    TRACE_DURATION("machina", "queue_notify", "device_id", DeviceId, "queue", queue, "flow_id",
                   flow_id);

    // Only emplace a new flow ID if there is no other still in flight.
    trace_async_id_t unset = 0;
    if (trace_flow_id(queue)->compare_exchange_strong(unset, flow_id)) {
      TRACE_FLOW_BEGIN("machina", "queue_signal", flow_id);
    }

    // Send an interrupt back to the guest if we've generated one while
    // processing the queue.
    zx_status_t status = this->pci_.Interrupt();
    if (status != ZX_OK) {
      return status;
    }

    // Notify threads waiting on a descriptor.
    return queues_[queue].Notify();
  }

  VirtioQueue* queue(uint16_t sel) { return sel >= NumQueues ? nullptr : &queues_[sel]; }

  std::atomic<trace_async_id_t>* trace_flow_id(uint16_t sel) {
    return sel >= NumQueues ? nullptr : &trace_flow_ids_[sel];
  }

 private:
  VirtioQueue queues_[NumQueues];
  // One flow ID slot for each device queue, used for IO correlation tracing.
  std::atomic<trace_async_id_t> trace_flow_ids_[NumQueues] = {};

  VirtioInprocessDevice(const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigQueueFn config_queue,
                        VirtioDeviceConfig::NotifyQueueFn notify_queue,
                        VirtioDeviceConfig::ConfigDeviceFn config_device,
                        VirtioDeviceConfig::ReadyDeviceFn ready_device)
      : VirtioDevice<DeviceId, NumQueues, ConfigType>(
            phys_mem, device_features, std::move(config_queue), std::move(notify_queue),
            std::move(config_device), std::move(ready_device)) {
    for (int i = 0; i < NumQueues; ++i) {
      queues_[i].set_phys_mem(&phys_mem);
      queues_[i].set_interrupt(
          fit::bind_member<zx_status_t, VirtioDevice<DeviceId, NumQueues, ConfigType>>(
              this, &VirtioInprocessDevice::Interrupt));
    }
  }

  zx_status_t ConfigQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                          zx_gpaddr_t used) {
    if (queue >= NumQueues) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    queues_[queue].Configure(size, desc, avail, used);
    return ZX_OK;
  }
};

// Interface for all virtio device components.
template <uint8_t DeviceId, uint16_t NumQueues, typename ConfigType>
class VirtioComponentDevice : public VirtioDevice<DeviceId, NumQueues, ConfigType> {
 protected:
  VirtioComponentDevice(const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigQueueFn config_queue,
                        VirtioDeviceConfig::ConfigDeviceFn config_device,
                        VirtioDeviceConfig::ReadyDeviceFn ready_device)
      : VirtioDevice<DeviceId, NumQueues, ConfigType>(
            phys_mem, device_features, std::move(config_queue), noop_notify_queue,
            std::move(config_device), std::move(ready_device)) {
    zx_status_t status = zx::event::create(0, &event_);
    FX_CHECK(status == ZX_OK) << "Failed to create event";
    event_koid_ = fsl::GetKoid(event_.get());
    wait_.set_object(event_.get());
    wait_.set_trigger(ZX_USER_SIGNAL_ALL);
  }

  VirtioComponentDevice(const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigQueueFn config_queue,
                        VirtioDeviceConfig::ReadyDeviceFn ready_device)
      : VirtioComponentDevice(phys_mem, device_features, std::move(config_queue),
                              noop_config_device, std::move(ready_device)) {}

  zx_status_t PrepStart(const zx::guest& guest, async_dispatcher_t* dispatcher,
                        fuchsia::virtualization::hardware::StartInfo* start_info) {
    zx_status_t status = wait_.Begin(dispatcher);
    if (status != ZX_OK) {
      return status;
    }
    if (!this->pci_.is_bar_implemented(kVirtioPciNotifyBar)) {
      return ZX_ERR_UNAVAILABLE;
    }
    const PciBar* bar = this->pci_.bar(kVirtioPciNotifyBar);
    start_info->trap = {.addr = bar->addr, .size = align(bar->size, PAGE_SIZE)};
    status = guest.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE, &start_info->guest);
    if (status != ZX_OK) {
      return status;
    }
    status = event().duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, &start_info->event);
    if (status != ZX_OK) {
      return status;
    }
    return this->phys_mem_.vmo().duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO | ZX_RIGHT_MAP,
                                           &start_info->vmo);
  }

  const zx::event& event() const { return event_; }

 private:
  void OnInterrupt(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                   const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return;
    }
    TRACE_FLOW_END("machina", "device:interrupt", event_koid_);
    status = event_.signal(signal->trigger, 0);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to clear interrupt signal " << status;
      return;
    }
    status = this->Interrupt(signal->observed >> kDeviceInterruptShift);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to raise device interrupt " << status;
      return;
    }
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to wait for interrupt " << status;
    }
  }

  zx::event event_;
  zx_koid_t event_koid_;
  async::WaitMethod<VirtioComponentDevice, &VirtioComponentDevice::OnInterrupt> wait_{this};
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_DEVICE_H_
