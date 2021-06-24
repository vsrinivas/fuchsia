// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
#include <lib/ddk/binding.h>

#include <vector>

#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "acpi/status.h"
#include "acpi/util.h"
#include "src/devices/board/drivers/x86/acpi/resources.h"

#define MAX_NAMESPACE_DEPTH 100

namespace acpi {
namespace internal {

template <typename T>
static inline size_t UnusedPropsCount(const T& props, uint32_t propcount) {
  ZX_DEBUG_ASSERT(propcount <= std::size(props));
  return std::size(props) - std::min<size_t>(propcount, std::size(props));
}

}  // namespace internal

// A free standing function which can be used to fetch the Info structure of an
// ACPI device.  It returns a acpi::status which either holds a managed pointer
// to the info object, or an ACPI error code in the case of failure.
acpi::status<UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj);

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
