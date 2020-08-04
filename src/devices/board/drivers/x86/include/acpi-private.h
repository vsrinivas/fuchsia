// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
#include <lib/fitx/result.h>

#include <vector>

#include <ddk/protocol/auxdata.h>
#include <ddk/protocol/pciroot.h>
#include <ddktl/device.h>
#include <ddktl/protocol/acpi.h>
#include <fbl/mutex.h>

#include "resources.h"

#define MAX_NAMESPACE_DEPTH 100

namespace acpi {

// An RAII unique pointer type for resources allocated from the ACPICA library.
template <typename T>
struct UniquePtrDeleter {
  void operator()(T* mem) { ACPI_FREE(mem); }
};

template <typename T>
using UniquePtr = std::unique_ptr<T, UniquePtrDeleter<T>>;

// A free standing function which can be used to fetch the Info structure of an
// ACPI device.  It returns a fitx::result which either holds a managed pointer
// to the info object, or an ACPI error code in the case of failure.
fitx::result<ACPI_STATUS, UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj);

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

// A utility function which can be used to invoke the ACPICA library's
// AcpiWalkNamespace function, but with an arbitrary Callable instead of needing
// to use C-style callbacks with context pointers.
enum class WalkDirection {
  Descending,
  Ascending,
};

template <typename Callable>
ACPI_STATUS WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object, uint32_t max_depth,
                          Callable cbk) {
  auto Descent = [](ACPI_HANDLE object, uint32_t level, void* ctx, void**) -> ACPI_STATUS {
    return (*static_cast<Callable*>(ctx))(object, level, WalkDirection::Descending);
  };

  auto Ascent = [](ACPI_HANDLE object, uint32_t level, void* ctx, void**) -> ACPI_STATUS {
    return (*static_cast<Callable*>(ctx))(object, level, WalkDirection::Ascending);
  };

  return ::AcpiWalkNamespace(type, start_object, max_depth, Descent, Ascent, &cbk, nullptr);
}

}  // namespace acpi

struct pci_child_auxdata_ctx_t {
  uint8_t max;
  uint8_t i;
  auxdata_i2c_device_t* data;
};

// TODO(cja): this is here because of kpci.cc and can be removed once
// kernel pci is out of the tree.
device_add_args_t get_device_add_args(const char* name, ACPI_DEVICE_INFO* info,
                                      std::array<zx_device_prop_t, 4>* out_props);

const zx_protocol_device_t* get_acpi_root_device_proto(void);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
