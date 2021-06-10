// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

namespace acpi {

acpi::status<> RealAcpi::WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object,
                                       uint32_t max_depth, NamespaceCallable cbk) {
  auto Descent = [](ACPI_HANDLE object, uint32_t level, void* ctx, void**) -> ACPI_STATUS {
    return (*static_cast<NamespaceCallable*>(ctx))(object, level, WalkDirection::Descending)
        .status_value();
  };

  auto Ascent = [](ACPI_HANDLE object, uint32_t level, void* ctx, void**) -> ACPI_STATUS {
    return (*static_cast<NamespaceCallable*>(ctx))(object, level, WalkDirection::Ascending)
        .status_value();
  };

  return acpi::make_status(
      ::AcpiWalkNamespace(type, start_object, max_depth, Descent, Ascent, &cbk, nullptr));
}

acpi::status<> RealAcpi::WalkResources(ACPI_HANDLE object, const char* resource_name,
                                       ResourcesCallable cbk) {
  auto Thunk = [](ACPI_RESOURCE* res, void* ctx) -> ACPI_STATUS {
    return (*static_cast<Acpi::ResourcesCallable*>(ctx))(res).status_value();
  };
  return acpi::make_status(
      ::AcpiWalkResources(object, const_cast<char*>(resource_name), Thunk, &cbk));
}

acpi::status<> RealAcpi::GetDevices(const char* hid, DeviceCallable cbk) {
  auto Thunk = [](ACPI_HANDLE object, uint32_t level, void* ctx, void**) -> ACPI_STATUS {
    return (*static_cast<DeviceCallable*>(ctx))(object, level).status_value();
  };

  return acpi::make_status(::AcpiGetDevices(const_cast<char*>(hid), Thunk, &cbk, nullptr));
}
}  // namespace acpi
