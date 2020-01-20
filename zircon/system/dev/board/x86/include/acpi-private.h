// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_INCLUDE_ACPI_PRIVATE_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_INCLUDE_ACPI_PRIVATE_H_
#include <vector>

#include <ddk/device.h>
#include <ddk/protocol/acpi.h>
#include <ddk/protocol/auxdata.h>
#include <ddk/protocol/pciroot.h>

#include "resources.h"

#define MAX_NAMESPACE_DEPTH 100

struct AcpiDevicePioResource {
  explicit AcpiDevicePioResource(const resource_io& io)
      : base_address{io.minimum}, alignment{io.alignment}, address_length{io.address_length} {}

  uint32_t base_address;
  uint32_t alignment;
  uint32_t address_length;
};

struct AcpiDeviceMmioResource {
  AcpiDeviceMmioResource(bool writeable, uint32_t base_address, uint32_t alignment,
                         uint32_t address_length)
      : writeable{writeable},
        base_address{base_address},
        alignment{alignment},
        address_length{address_length} {}

  explicit AcpiDeviceMmioResource(const resource_memory_t& mem)
      : AcpiDeviceMmioResource{mem.writeable, mem.minimum, mem.alignment, mem.address_length} {}

  bool writeable;
  uint32_t base_address;
  uint32_t alignment;
  uint32_t address_length;
};

struct AcpiDeviceIrqResource {
  AcpiDeviceIrqResource(const resource_irq irq, int pin_index)
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

struct acpi_device_t {
  zx_device_t* zxdev;
  zx_device_t* platform_bus;

  mutable fbl::Mutex lock;

  bool got_resources;

  // port resources from _CRS
  std::vector<AcpiDevicePioResource> pio_resources;

  // memory resources from _CRS
  std::vector<AcpiDeviceMmioResource> mmio_resources;

  // interrupt resources from _CRS
  std::vector<AcpiDeviceIrqResource> irqs;

  // handle to the corresponding ACPI node
  ACPI_HANDLE ns_node;

  zx_status_t ReportCurrentResources();
  ACPI_STATUS AddResource(ACPI_RESOURCE*);
  zx_status_t AcpiOpGetPioLocked(uint32_t index, zx_handle_t* out_pio);
  zx_status_t AcpiOpGetMmioLocked(uint32_t index, acpi_mmio* out_mmio);
  zx_status_t AcpiOpMapInterruptLocked(int64_t which_irq, zx_handle_t* out_handle);
  zx_status_t AcpiOpConnectSysmemLocked(zx_handle_t handle) const;
  zx_status_t AcpiOpRegisterSysmemHeapLocked(uint64_t heap, zx_handle_t handle) const;
};

struct publish_acpi_device_ctx_t {
  zx_device_t* sys_root;
  zx_device_t* acpi_root;
  zx_device_t* platform_bus;
  bool found_pci;
  uint8_t last_pci;  // bus number of the last PCI root seen
};

struct pci_child_auxdata_ctx_t {
  uint8_t max;
  uint8_t i;
  auxdata_i2c_device_t* data;
};

// TODO(cja): this is here because of kpci.cc and can be removed once
// kernel pci is out of the tree.
zx_device_t* publish_device(zx_device_t* parent, zx_device_t* platform_bus, ACPI_HANDLE handle,
                            ACPI_DEVICE_INFO* info, const char* name, uint32_t protocol_id,
                            void* protocol_ops);

const zx_protocol_device_t* get_acpi_root_device_proto(void);

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_INCLUDE_ACPI_PRIVATE_H_
