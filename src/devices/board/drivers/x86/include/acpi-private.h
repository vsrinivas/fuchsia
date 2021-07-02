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

}  // namespace acpi

const zx_protocol_device_t* get_acpi_root_device_proto(void);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_PRIVATE_H_
