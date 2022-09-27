// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_DEVICE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_DEVICE_H_

#include <fidl/fuchsia.virtualization.hardware/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace-engine/types.h>
#include <lib/trace/event.h>

#include <atomic>

#include "src/lib/fsl/handles/object_info.h"
#include "src/virtualization/bin/vmm/controller/realm_utils.h"
#include "src/virtualization/bin/vmm/device/config.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"
#include "src/virtualization/bin/vmm/virtio_pci.h"

// Set of features that are supported transparently for all devices.
static constexpr uint32_t kVirtioFeatures = 0;

constexpr zx_status_t noop_notify_queue(uint16_t queue) { return ZX_OK; }
constexpr zx_status_t noop_config_device(uint64_t addr, const IoValue& value) { return ZX_OK; }

// Interface for all virtio device components.
template <uint8_t DeviceId, uint16_t NumQueues, typename ConfigType>
class VirtioComponentDevice {
 public:
  PciDevice* pci_device() { return &pci_; }

 protected:
  VirtioComponentDevice(std::string_view name, const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigQueueFn config_queue,
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
            .notify_queue = noop_notify_queue,
            .config_device = std::move(config_device),
            .ready_device = std::move(ready_device),
        },
        pci_(&device_config_, name) {
    zx_status_t status = zx::event::create(0, &event_);
    FX_CHECK(status == ZX_OK) << "Failed to create event";
    event_koid_ = fsl::GetKoid(event_.get());
    wait_.set_object(event_.get());
    wait_.set_trigger(ZX_USER_SIGNAL_ALL);
  }

  VirtioComponentDevice(std::string_view name, const PhysMem& phys_mem, uint32_t device_features,
                        VirtioDeviceConfig::ConfigQueueFn config_queue,
                        VirtioDeviceConfig::ReadyDeviceFn ready_device)
      : VirtioComponentDevice(name, phys_mem, device_features, std::move(config_queue),
                              noop_config_device, std::move(ready_device)) {}

  ~VirtioComponentDevice() {
    if (realm_.is_bound()) {
      ::fuchsia::component::Realm_DestroyChild_Result result;
      realm_->DestroyChild({.name = component_name_, .collection = collection_name_}, &result);
    }
  }

  zx_status_t PrepStart(const zx::guest& guest, async_dispatcher_t* dispatcher,
                        fuchsia::virtualization::hardware::StartInfo* start_info) {
    zx_status_t status = wait_.Begin(dispatcher);
    if (status != ZX_OK) {
      return status;
    }

    // Communicate the allocated notify BAR address/size to the component.
    const PciBar& bar = this->pci_.notify_bar();
    ZX_DEBUG_ASSERT(bar.addr() != 0);  // BAR address should have been allocated by now.
    start_info->trap = {.addr = bar.addr(), .size = align(bar.size(), PAGE_SIZE)};

    // Give the component access to the guest and guest memory.
    status = guest.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE, &start_info->guest);
    if (status != ZX_OK) {
      return status;
    }
    status = event().duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, &start_info->event);
    if (status != ZX_OK) {
      return status;
    }
    return this->phys_mem_.vmo().duplicate(
        ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &start_info->vmo);
  }

  zx_status_t PrepStart(const zx::guest& guest, async_dispatcher_t* dispatcher,
                        fuchsia_virtualization_hardware::wire::StartInfo* start_info) {
    fuchsia::virtualization::hardware::StartInfo hlcpp_start_info;
    zx_status_t status = PrepStart(guest, dispatcher, &hlcpp_start_info);
    if (status == ZX_OK) {
      start_info->trap.addr = hlcpp_start_info.trap.addr;
      start_info->trap.size = hlcpp_start_info.trap.size;
      start_info->guest = std::move(hlcpp_start_info.guest);
      start_info->event = std::move(hlcpp_start_info.event);
      start_info->vmo = std::move(hlcpp_start_info.vmo);
    }
    return status;
  }

  const zx::event& event() const { return event_; }

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

  zx_status_t CreateDynamicComponent(
      ::sys::ComponentContext* context, const char* collection_name, const char* component_name,
      const char* component_url,
      fit::function<zx_status_t(std::shared_ptr<sys::ServiceDirectory> services)> callback) {
    component_name_ = component_name;
    collection_name_ = collection_name;

    zx_status_t status = context->svc()->Connect(realm_.NewRequest());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Virtio device controller failed to connect to realm";
      return status;
    }

    // TODO(fxbug.dev/104989): Move this function entirely into the VirtioComponentDevice.
    return ::CreateDynamicComponent(realm_, collection_name, component_name, component_url,
                                    std::move(callback));
  }

  const PhysMem& phys_mem_;
  ConfigType config_ __TA_GUARDED(device_config_.mutex) = {};
  VirtioDeviceConfig device_config_;
  VirtioPci pci_;
  VirtioQueueConfig queue_configs_[NumQueues] __TA_GUARDED(device_config_.mutex) = {};

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

  std::string component_name_;
  std::string collection_name_;
  ::fuchsia::component::RealmSyncPtr realm_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_DEVICE_H_
