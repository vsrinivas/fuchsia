// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_ACPI_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_ACPI_H_

#include <functional>

#include <acpica/acpi.h>

#include "status.h"

namespace acpi {

enum WalkDirection {
  Descending,
  Ascending,
};

// Wrapper class used to interface with ACPICA (in the real system),
// or a mock ACPI implementation (in tests).
class Acpi {
 public:
  virtual ~Acpi() = default;
  // A utility function which can be used to invoke the ACPICA library's
  // AcpiWalkNamespace function, but with an arbitrary Callable instead of needing
  // to use C-style callbacks with context pointers.
  using NamespaceCallable =
      std::function<acpi::status<>(ACPI_HANDLE object, uint32_t level, WalkDirection dir)>;
  virtual acpi::status<> WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object,
                                       uint32_t max_depth, NamespaceCallable cbk) = 0;
  // A utility function which can be used to invoke the ACPICA library's
  // AcpiWalkResources function, but with an arbitrary Callable instead of needing
  // to use C-style callbacks with context pointers.
  using ResourcesCallable = std::function<acpi::status<>(ACPI_RESOURCE* res)>;
  virtual acpi::status<> WalkResources(ACPI_HANDLE object, const char* resource_name,
                                       ResourcesCallable cbk) = 0;
};

// Implementation of `Acpi` using ACPICA to operate on real ACPI tables.
class RealAcpi : public Acpi {
 public:
  ~RealAcpi() override = default;
  acpi::status<> WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object, uint32_t max_depth,
                               NamespaceCallable cbk) override;
  acpi::status<> WalkResources(ACPI_HANDLE object, const char* resource_name,
                               ResourcesCallable cbk) override;
};
}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_ACPI_H_
