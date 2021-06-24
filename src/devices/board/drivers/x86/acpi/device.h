// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <lib/ddk/binding.h>

#include <acpica/acpi.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "src/devices/board/drivers/x86/acpi/resources.h"

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

class Device;
using DeviceType = ddk::Device<::acpi::Device>;
class Device : public DeviceType, public ddk::AcpiProtocol<Device, ddk::base_protocol> {
 public:
  Device(zx_device_t* parent, ACPI_HANDLE acpi_handle, zx_device_t* platform_bus)
      : DeviceType{parent}, acpi_handle_{acpi_handle}, platform_bus_{platform_bus} {}

  // DDK mix-in impls.
  void DdkRelease() { delete this; }

  ACPI_HANDLE acpi_handle() const { return acpi_handle_; }
  zx_device_t* platform_bus() const { return platform_bus_; }
  zx_device_t** mutable_zxdev() { return &zxdev_; }

  zx_status_t AcpiGetPio(uint32_t index, zx::resource* out_pio);
  zx_status_t AcpiGetMmio(uint32_t index, acpi_mmio* out_mmio);
  zx_status_t AcpiMapInterrupt(int64_t which_irq, zx::interrupt* handle);
  zx_status_t AcpiGetBti(uint32_t bdf, uint32_t index, zx::bti* bti);
  zx_status_t AcpiConnectSysmem(zx::channel connection);
  zx_status_t AcpiRegisterSysmemHeap(uint64_t heap, zx::channel connection);

 private:
  // Handle to the corresponding ACPI node
  ACPI_HANDLE acpi_handle_;

  zx_device_t* platform_bus_;

  mutable fbl::Mutex lock_;
  bool got_resources_ = false;

  // Port, memory, and interrupt resources from _CRS respectively
  std::vector<DevicePioResource> pio_resources_;
  std::vector<DeviceMmioResource> mmio_resources_;
  std::vector<DeviceIrqResource> irqs_;

  zx_status_t ReportCurrentResources();
  ACPI_STATUS AddResource(ACPI_RESOURCE*);
};

}  // namespace acpi
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_H_
