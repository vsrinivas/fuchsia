// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
#include <lib/fitx/result.h>

#include <vector>

#include <ddk/binding.h>
#include <ddktl/device.h>
#include <ddktl/protocol/acpi.h>
#include <fbl/mutex.h>

#include "resources.h"

#define MAX_NAMESPACE_DEPTH 100

namespace acpi {
namespace internal {

// utility functions used to implement ExtractHidToDevProps and
// ExtractCidToDevProps (below)
static inline uint32_t ExtractPnpIdWord(const ACPI_PNP_DEVICE_ID& id, size_t offset) {
  auto buf = reinterpret_cast<const char*>(id.String);
  auto buf_len = static_cast<size_t>(id.Length);

  if (offset >= buf_len) {
    return 0;
  }

  size_t i;
  size_t avail = buf_len - offset;
  uint32_t ret = buf[offset];
  for (i = 1; i < std::min(avail, sizeof(uint32_t)); ++i) {
    ret = (ret << 8) | buf[offset + i];
  }
  ret <<= (sizeof(uint32_t) - i) * 8;

  return ret;
}

template <typename T>
static inline size_t UnusedPropsCount(const T& props, uint32_t propcount) {
  ZX_DEBUG_ASSERT(propcount <= std::size(props));
  return std::size(props) - std::min<size_t>(propcount, std::size(props));
}

}  // namespace internal

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

// A utility function which can be used to invoke the ACPICA library's
// AcpiWalkResources function, but with an arbitrary Callable instead of needing
// to use C-style callbacks with context pointers.
template <typename Callable>
ACPI_STATUS WalkResources(ACPI_HANDLE object, const char* resource_name, Callable cbk) {
  auto Thunk = [](ACPI_RESOURCE* res, void* ctx) -> ACPI_STATUS {
    return (*static_cast<Callable*>(ctx))(res);
  };
  return ::AcpiWalkResources(object, const_cast<char*>(resource_name), Thunk, &cbk);
}

// ExtractHidToDevProps and ExtractCidToDevProps
//
// These functions will take an ACPI_DEVICE_INFO structure, and attempt to
// extract the Hardware ID or first available Compatiblity ID from the device
// (if present), and pack the data (space permitting) into an array of device
// properties which exists in an arbitrary structure provided by the user.
//
// The string HID/CID provided by ACPI will be packed into a pair of uint32_t in
// a big-endian fashion, with 0x00 being used as a filler in the case that the
// identifier is too short.  Appropriate device property type labels will be
// applied to the dev_props structure.
//
// ACPI IDs which are longer than 8 bytes are currently unsupported as there is
// no good way to represent them with the existing devprops binding scheme.
//
template <typename T>
zx_status_t ExtractHidToDevProps(const ACPI_DEVICE_INFO& info, T& props, uint32_t& propcount) {
  // If we have no HID to extract, then just do nothing.  This is not
  // considered to be an error.
  if (!((info.Valid & ACPI_VALID_HID) && (info.HardwareId.Length > 0))) {
    return ZX_OK;
  }

  const ACPI_PNP_DEVICE_ID& pnp_id = info.HardwareId;

  // We cannot currently handle any IDs which would be longer than 8 bytes
  // (not including the null termination of the string)
  if ((pnp_id.Length - 1) > sizeof(uint64_t)) {
    return ZX_ERR_INTERNAL;
  }

  // Make sure we have the space to extract the info.
  if (internal::UnusedPropsCount(props, propcount) < 2) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  props[propcount].id = BIND_ACPI_HID_0_3;
  props[propcount++].value = internal::ExtractPnpIdWord(pnp_id, 0);
  props[propcount].id = BIND_ACPI_HID_4_7;
  props[propcount++].value = internal::ExtractPnpIdWord(pnp_id, 4);

  return ZX_OK;
}

template <typename T>
zx_status_t ExtractCidToDevProps(const ACPI_DEVICE_INFO& info, T& props, uint32_t& propcount) {
  // If we have no CID to extract, then just do nothing.  This is not
  // considered to be an error.
  if (!((info.Valid & ACPI_VALID_CID) && (info.CompatibleIdList.Count > 0) &&
        (info.CompatibleIdList.Ids[0].Length > 0))) {
    return ZX_OK;
  }

  const ACPI_PNP_DEVICE_ID& pnp_id = info.CompatibleIdList.Ids[0];

  // We cannot currently handle any IDs which would be longer than 8 bytes
  // (not including the null termination of the string)
  if ((pnp_id.Length - 1) > sizeof(uint64_t)) {
    return ZX_ERR_INTERNAL;
  }

  // Make sure we have the space to extract the info.
  if (internal::UnusedPropsCount(props, propcount) < 2) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  props[propcount].id = BIND_ACPI_CID_0_3;
  props[propcount++].value = internal::ExtractPnpIdWord(pnp_id, 0);
  props[propcount].id = BIND_ACPI_CID_4_7;
  props[propcount++].value = internal::ExtractPnpIdWord(pnp_id, 4);

  return ZX_OK;
}

}  // namespace acpi

// TODO(cja): this is here because of kpci.cc and can be removed once
// kernel pci is out of the tree.
device_add_args_t get_device_add_args(const char* name, ACPI_DEVICE_INFO* info,
                                      std::array<zx_device_prop_t, 4>* out_props);

const zx_protocol_device_t* get_acpi_root_device_proto(void);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
