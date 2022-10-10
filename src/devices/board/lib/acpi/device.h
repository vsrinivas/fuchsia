// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_H_
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/ddk/binding.h>
#include <lib/fpromise/promise.h>
#include <lib/svc/outgoing.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <acpica/acpi.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "lib/ddk/device.h"
#include "src/devices/board/lib/acpi/device-args.h"
#include "src/devices/board/lib/acpi/event.h"
#include "src/devices/board/lib/acpi/manager.h"
#include "src/devices/board/lib/acpi/power-resource.h"
#include "src/devices/board/lib/acpi/resources.h"

#ifndef __Fuchsia__
#error "Use device-for-host.h!"
#endif

namespace acpi {

struct DevicePioResource {
  explicit DevicePioResource(const resource_io& io)
      : base_address{io.minimum}, alignment{io.alignment}, address_length{io.address_length} {}

  uint32_t base_address;
  uint32_t alignment;
  uint32_t address_length;
};

struct DeviceMmioResource {
  DeviceMmioResource(bool writeable, uint32_t base_address, uint32_t alignment,
                     uint32_t address_length)
      : writeable{writeable},
        base_address{base_address},
        alignment{alignment},
        address_length{address_length} {}

  explicit DeviceMmioResource(const resource_memory_t& mem)
      : DeviceMmioResource{mem.writeable, mem.minimum, mem.alignment, mem.address_length} {}

  bool writeable;
  uint32_t base_address;
  uint32_t alignment;
  uint32_t address_length;
};

struct DeviceIrqResource {
  DeviceIrqResource(const resource_irq irq, int pin_index)
      : trigger{irq.trigger},
        polarity{irq.polarity},
        sharable{irq.sharable},
        wake_capable{irq.wake_capable},
        pin{static_cast<uint8_t>(irq.pins[pin_index])} {}

  uint8_t trigger;
#define ACPI_IRQ_TRIGGER_LEVEL 0
#define ACPI_IRQ_TRIGGER_EDGE 1
  uint8_t polarity;
#define ACPI_IRQ_ACTIVE_HIGH 0
#define ACPI_IRQ_ACTIVE_LOW 1
#define ACPI_IRQ_ACTIVE_BOTH 2
  uint8_t sharable;
#define ACPI_IRQ_EXCLUSIVE 0
#define ACPI_IRQ_SHARED 1
  uint8_t wake_capable;
  uint8_t pin;
};

struct PowerStateTransitionResponse {
  PowerStateTransitionResponse(zx_status_t status, uint8_t out_state)
      : status{status}, out_state{out_state} {}
  zx_status_t status;
  uint8_t out_state;
};

struct DevicePowerState {
  DevicePowerState(uint8_t state, std::unordered_set<uint8_t> supported_s_states)
      : state{state}, supported_s_states{std::move(supported_s_states)} {}
  uint8_t state;
  std::unordered_set<uint8_t> supported_s_states;
};

class Device;
using DeviceType = ddk::Device<::acpi::Device, ddk::Initializable, ddk::Unbindable,
                               ddk::Messageable<fuchsia_hardware_acpi::Device>::Mixin>;
class Device : public DeviceType,
               public fidl::WireAsyncEventHandler<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  explicit Device(DeviceArgs&& args) : Device(args) {}
  explicit Device(DeviceArgs& args)
      : DeviceType{args.parent_},
        manager_{args.manager_},
        acpi_{manager_->acpi()},
        acpi_handle_{args.handle_},
        metadata_{std::move(args.metadata_)},
        bus_type_{args.bus_type_},
        bus_id_{args.bus_id_},
        pci_bdfs_{std::move(args.bdfs_)} {}

  // DDK mix-in impls.
  void DdkRelease() { delete this; }
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);

  ACPI_HANDLE acpi_handle() const { return acpi_handle_; }
  zx_device_t** mutable_zxdev() { return &zxdev_; }

  void AcpiConnectServer(zx::channel server);

  // Wrapper around |DdkAdd| which handles setting up FIDL outgoing directory.
  zx::status<> AddDevice(const char* name, cpp20::span<zx_device_prop_t> props,
                         cpp20::span<zx_device_str_prop_t> str_props, uint32_t flags);

  zx::status<zx::interrupt> GetInterrupt(size_t index) __TA_EXCLUDES(lock_);

  // FIDL impls
  void GetBusId(GetBusIdCompleter::Sync& completer) override;
  void EvaluateObject(EvaluateObjectRequestView request,
                      EvaluateObjectCompleter::Sync& completer) override;
  void MapInterrupt(MapInterruptRequestView request,
                    MapInterruptCompleter::Sync& completer) override;
  void GetPio(GetPioRequestView request, GetPioCompleter::Sync& completer) override;
  void GetMmio(GetMmioRequestView request, GetMmioCompleter::Sync& completer) override;
  void GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) override;
  void InstallNotifyHandler(InstallNotifyHandlerRequestView request,
                            InstallNotifyHandlerCompleter::Sync& completer) override;
  void RemoveNotifyHandler(RemoveNotifyHandlerCompleter::Sync& completer) override;
  void AcquireGlobalLock(AcquireGlobalLockCompleter::Sync& completer) override;
  void InstallAddressSpaceHandler(InstallAddressSpaceHandlerRequestView request,
                                  InstallAddressSpaceHandlerCompleter::Sync& completer) override;
  void SetWakeDevice(SetWakeDeviceRequestView request,
                     SetWakeDeviceCompleter::Sync& completer) override;

  std::vector<pci_bdf_t>& pci_bdfs() { return pci_bdfs_; }

  ACPI_STATUS RemoveNotifyHandler();

  // Returns a map containing information on D states supported by this device.
  std::unordered_map<uint8_t, DevicePowerState> GetSupportedPowerStates();
  // Attempts to transition a device to the given D state. Returns the resulting D state of the
  // device.
  PowerStateTransitionResponse TransitionToPowerState(uint8_t requested_state);

 private:
  struct HandlerCtx {
    Device* device;
    uint32_t space_type;
  };

  static ACPI_STATUS AddressSpaceHandler(uint32_t function, ACPI_PHYSICAL_ADDRESS physical_address,
                                         uint32_t bit_width, UINT64* value, void* handler_ctx,
                                         void* region_ctx);
  static void DeviceObjectNotificationHandler(ACPI_HANDLE object, uint32_t value, void* context);
  zx_status_t ReportCurrentResources() __TA_REQUIRES(lock_);
  ACPI_STATUS AddResource(ACPI_RESOURCE*) __TA_REQUIRES(lock_);
  // Set up FIDL outgoing directory and start serving fuchsia.hardware.acpi.Device.
  zx::status<zx::channel> PrepareOutgoing();

  struct PowerStateInfo {
    uint8_t d_state;
    // This should be sorted by ascending resource_order.
    std::vector<ACPI_HANDLE> power_resources;
    bool defines_psx_method = false;
    std::unordered_set<uint8_t> supported_s_states;
  };

  zx_status_t InitializePowerManagement();
  zx::status<PowerStateInfo> GetInfoForState(uint8_t d_state);
  zx_status_t ConfigureInitialPowerState();
  zx_status_t CallPsxMethod(const PowerStateInfo& state);
  zx_status_t Resume(const PowerStateInfo& requested_state_info);
  zx_status_t Suspend(const PowerStateInfo& requested_state_info);

  PowerStateInfo* GetPowerStateInfo(uint8_t d_state) {
    auto power_state = supported_power_states_.find(d_state);
    if (power_state == supported_power_states_.end()) {
      return nullptr;
    }
    return &power_state->second;
  }

  acpi::Manager* manager_;
  acpi::Acpi* acpi_;
  // Handle to the corresponding ACPI node
  ACPI_HANDLE acpi_handle_;
  // BTI ID for dummy IOMMU.
  uint32_t bti_id_ = manager_->GetNextBtiId();

  mutable std::mutex lock_;
  bool got_resources_ = false;

  // Port, memory, and interrupt resources from _CRS respectively
  std::vector<DevicePioResource> pio_resources_ __TA_GUARDED(lock_);
  std::vector<DeviceMmioResource> mmio_resources_ __TA_GUARDED(lock_);
  std::vector<DeviceIrqResource> irqs_ __TA_GUARDED(lock_);

  bool can_use_global_lock_ = false;

  std::unordered_map<uint8_t, PowerStateInfo> supported_power_states_;
  uint8_t current_power_state_ = DEV_POWER_STATE_D3COLD;

  // FIDL-encoded child metadata.
  std::vector<uint8_t> metadata_;
  BusType bus_type_ = BusType::kUnknown;
  uint32_t bus_id_ = UINT32_MAX;

  // TODO(fxbug.dev/32978): remove once kernel PCI is no longer used.
  std::vector<pci_bdf_t> pci_bdfs_;

  // ACPI events.
  std::optional<fidl::WireSharedClient<fuchsia_hardware_acpi::NotifyHandler>> notify_handler_;
  std::atomic_size_t pending_notify_count_ = 0;
  std::optional<fpromise::promise<void>> notify_teardown_finished_;
  std::atomic_bool notify_handler_active_ = false;
  uint32_t notify_handler_type_;
  bool notify_count_warned_ = false;

  // ACPI address space handling.
  std::mutex address_handler_lock_;
  std::unordered_map<uint32_t, fidl::WireSharedClient<fuchsia_hardware_acpi::AddressSpaceHandler>>
      address_handlers_ __TA_GUARDED(address_handler_lock_);
  std::vector<fpromise::promise<void>> address_handler_teardown_finished_
      __TA_GUARDED(address_handler_lock_);

  std::optional<svc::Outgoing> outgoing_;

  // Passthrough device -- the one that drivers actually bind to. This is a child of this |Device|
  // instance.
  zx_device_t* passthrough_dev_;
};

}  // namespace acpi
#endif  // SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_H_
