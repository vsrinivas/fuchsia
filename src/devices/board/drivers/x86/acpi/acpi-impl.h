// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_ACPI_IMPL_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_ACPI_IMPL_H_
#include <optional>
#include <vector>

#include <acpica/acpi.h>

#include "acpi.h"
#include "status.h"
#include "util.h"

namespace acpi {

// Implementation of `Acpi` using ACPICA to operate on real ACPI tables.
class AcpiImpl : public Acpi {
 public:
  ~AcpiImpl() override = default;
  acpi::status<> WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object, uint32_t max_depth,
                               NamespaceCallable cbk) override;
  acpi::status<> WalkResources(ACPI_HANDLE object, const char* resource_name,
                               ResourcesCallable cbk) override;

  acpi::status<> GetDevices(const char* hid, DeviceCallable cbk) override;

  acpi::status<acpi::UniquePtr<ACPI_OBJECT>> EvaluateObject(
      ACPI_HANDLE object, const char* pathname,
      std::optional<std::vector<ACPI_OBJECT>> args) override;

  acpi::status<acpi::UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj) override;

  acpi::status<ACPI_HANDLE> GetParent(ACPI_HANDLE child) override;
  acpi::status<ACPI_HANDLE> GetHandle(ACPI_HANDLE parent, const char* pathname) override;
  acpi::status<std::string> GetPath(ACPI_HANDLE object) override;
};
}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_ACPI_IMPL_H_
