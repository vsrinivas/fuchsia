// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/fpromise/promise.h>

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

class Device;
using DeviceType = ddk::Device<::acpi::Device, ddk::Initializable, ddk::Unbindable,
                               ddk::Messageable<fuchsia_hardware_acpi::Device>::Mixin>;
class Device : public DeviceType,
               public ddk::AcpiProtocol<Device, ddk::base_protocol>,
               public fidl::WireAsyncEventHandler<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  Device(acpi::Manager* manager, zx_device_t* parent, ACPI_HANDLE acpi_handle)
      : DeviceType{parent}, manager_{manager}, acpi_{manager->acpi()}, acpi_handle_{acpi_handle} {}

  Device(acpi::Manager* manager, zx_device_t* parent, ACPI_HANDLE acpi_handle,
         std::vector<uint8_t> metadata, BusType bus_type, uint32_t bus_id)
      : DeviceType{parent},
        manager_{manager},
        acpi_{manager->acpi()},
        acpi_handle_{acpi_handle},
        metadata_{std::move(metadata)},
        bus_type_{bus_type},
        bus_id_{bus_id} {}

  Device(acpi::Manager* manager, zx_device_t* parent, ACPI_HANDLE acpi_handle,
         std::vector<pci_bdf_t> pci_bdfs)
      : DeviceType{parent},
        manager_{manager},
        acpi_{manager->acpi()},
        acpi_handle_{acpi_handle},
        pci_bdfs_{std::move(pci_bdfs)} {}

  // DDK mix-in impls.
  void DdkRelease() { delete this; }
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);

  ACPI_HANDLE acpi_handle() const { return acpi_handle_; }
  zx_device_t** mutable_zxdev() { return &zxdev_; }

  zx_status_t AcpiGetBti(uint32_t bdf, uint32_t index, zx::bti* bti);
  void AcpiConnectServer(zx::channel server);

  // FIDL impls
  void GetBusId(GetBusIdRequestView request, GetBusIdCompleter::Sync& completer) override;
  void EvaluateObject(EvaluateObjectRequestView request,
                      EvaluateObjectCompleter::Sync& completer) override;
  void MapInterrupt(MapInterruptRequestView request,
                    MapInterruptCompleter::Sync& completer) override;
  void GetPio(GetPioRequestView request, GetPioCompleter::Sync& completer) override;
  void GetMmio(GetMmioRequestView request, GetMmioCompleter::Sync& completer) override;
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

  acpi::Manager* manager_;
  acpi::Acpi* acpi_;
  // Handle to the corresponding ACPI node
  ACPI_HANDLE acpi_handle_;

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
};

}  // namespace acpi
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
