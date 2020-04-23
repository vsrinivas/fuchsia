// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_ACPI_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_ACPI_H_

#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>

namespace cros_ec {

// Thin wrapper around the ACPI library wrapping notifications.
//
// Used to facilitate mocks and tests.
//
// Thread compatible.
class AcpiHandle {
 public:
  // Cancels all registered notifications associated with this object.
  //
  // Blocks until all in-flight handlers have completed.
  virtual ~AcpiHandle() {}

  // Install a notification handler for this handle.
  virtual zx_status_t InstallNotifyHandler(UINT32 handler_type, ACPI_NOTIFY_HANDLER handler,
                                           void *context) = 0;

  // Remove notification handler, if one is installed.
  //
  // If a handler is already running when RemoveHandler is called, this will block
  // until the handler is finished.
  virtual void RemoveHandler() = 0;
};

// Create an AcpiHandle from the given raw ACPI_HANDLE.
std::unique_ptr<AcpiHandle> CreateAcpiHandle(ACPI_HANDLE handle);

// Create a no-op AcpiHandle.
std::unique_ptr<AcpiHandle> CreateNoOpAcpiHandle();

}  // namespace cros_ec

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_ACPI_H_
