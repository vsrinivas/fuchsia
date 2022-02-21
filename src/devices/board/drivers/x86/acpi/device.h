// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/fpromise/promise.h>
#include <lib/svc/outgoing.h>

#include <mutex>
#include <utility>

#include <acpica/acpi.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "src/devices/board/drivers/x86/acpi/event.h"
#include "src/devices/board/drivers/x86/acpi/manager.h"
#include "src/devices/board/drivers/x86/acpi/resources.h"

namespace acpi {
const char* BusTypeToString(BusType t);

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

struct DeviceArgs {
  zx_device_t* parent_;
  acpi::Manager* manager_;
  ACPI_HANDLE handle_;

  // Bus metadata
  std::vector<uint8_t> metadata_;
  BusType bus_type_ = BusType::kUnknown;
  uint32_t bus_id_ = UINT32_MAX;

  // PCI metadata
  std::vector<pci_bdf_t> bdfs_;

  DeviceArgs(zx_device_t* parent, acpi::Manager* manager, ACPI_HANDLE handle)
      : parent_(parent), manager_(manager), handle_(handle) {}
  DeviceArgs(DeviceArgs&) = delete;

  DeviceArgs& SetBusMetadata(std::vector<uint8_t> metadata, BusType bus_type, uint32_t bus_id) {
    metadata_ = std::move(metadata);
    bus_type_ = bus_type;
    bus_id_ = bus_id;
    return *this;
  }
  DeviceArgs& SetPciMetadata(std::vector<pci_bdf_t> bdfs) {
    bdfs_ = std::move(bdfs);
    return *this;
  }
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

  // FIDL impls
  void GetBusId(GetBusIdRequestView request, GetBusIdCompleter::Sync& completer) override;
  void EvaluateObject(EvaluateObjectRequestView request,
                      EvaluateObjectCompleter::Sync& completer) override;
  void MapInterrupt(MapInterruptRequestView request,
                    MapInterruptCompleter::Sync& completer) override;
  void GetPio(GetPioRequestView request, GetPioCompleter::Sync& completer) override;
  void GetMmio(GetMmioRequestView request, GetMmioCompleter::Sync& completer) override;
  void GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) override;
  void InstallNotifyHandler(InstallNotifyHandlerRequestView request,
                            InstallNotifyHandlerCompleter::Sync& completer) override;
  void AcquireGlobalLock(AcquireGlobalLockRequestView request,
                         AcquireGlobalLockCompleter::Sync& completer) override;
  void InstallAddressSpaceHandler(InstallAddressSpaceHandlerRequestView request,
                                  InstallAddressSpaceHandlerCompleter::Sync& completer) override;

  std::vector<pci_bdf_t>& pci_bdfs() { return pci_bdfs_; }

  void RemoveNotifyHandler();

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
};

}  // namespace acpi
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
