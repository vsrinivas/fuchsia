// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <zircon/types.h>

#include <memory>
#include <optional>

#include <acpica/acpi.h>

#include "../../include/errors.h"

namespace cros_ec {

// Thin wrapper around the ACPI library to facilitate mocks and tests.
class RealAcpiHandle : public AcpiHandle {
 public:
  RealAcpiHandle(ACPI_HANDLE handle) : handle_(handle) {}

  ~RealAcpiHandle() { RemoveHandler(); }

  zx_status_t InstallNotifyHandler(UINT32 handler_type, ACPI_NOTIFY_HANDLER handler,
                                   void *context) override {
    ZX_ASSERT_MSG(!notification_handler_.has_value(), "Handler already installed.");
    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(handle_, handler_type, handler, context);
    if (acpi_status != AE_OK) {
      return acpi_to_zx_status(acpi_status);
    }

    notification_handler_.emplace(Handler{/*handler_type=*/handler_type, /*handler=*/handler});
    return ZX_OK;
  }

  void RemoveHandler() override {
    if (notification_handler_.has_value()) {
      AcpiRemoveNotifyHandler(handle_, notification_handler_->handler_type,
                              notification_handler_->handler);
      notification_handler_.reset();
    }
  }

 private:
  // Notification handler details.
  struct Handler {
    UINT32 handler_type;
    ACPI_NOTIFY_HANDLER handler;
  };

  // The raw ACPI handler being managed.
  ACPI_HANDLE handle_;

  // If non-null, details about an ACPI notification handler that has been installed.
  std::optional<Handler> notification_handler_;
};

std::unique_ptr<AcpiHandle> CreateAcpiHandle(ACPI_HANDLE handle) {
  return std::make_unique<RealAcpiHandle>(handle);
}

// No-op AcpiHandle, suitable for testing.
class NoOpAcpiHandle : public AcpiHandle {
 public:
  NoOpAcpiHandle() {}

  ~NoOpAcpiHandle() {}

  zx_status_t InstallNotifyHandler(UINT32 handler_type, ACPI_NOTIFY_HANDLER handler,
                                   void *context) override {
    return ZX_OK;
  }

  void RemoveHandler() override {}
};

std::unique_ptr<AcpiHandle> CreateNoOpAcpiHandle() { return std::make_unique<NoOpAcpiHandle>(); }

}  // namespace cros_ec
