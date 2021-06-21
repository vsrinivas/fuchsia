// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi-impl.h"

#include <vector>

namespace acpi {

acpi::status<> AcpiImpl::WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object,
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

acpi::status<> AcpiImpl::WalkResources(ACPI_HANDLE object, const char* resource_name,
                                       ResourcesCallable cbk) {
  auto Thunk = [](ACPI_RESOURCE* res, void* ctx) -> ACPI_STATUS {
    return (*static_cast<Acpi::ResourcesCallable*>(ctx))(res).status_value();
  };
  return acpi::make_status(
      ::AcpiWalkResources(object, const_cast<char*>(resource_name), Thunk, &cbk));
}

acpi::status<> AcpiImpl::GetDevices(const char* hid, DeviceCallable cbk) {
  auto Thunk = [](ACPI_HANDLE object, uint32_t level, void* ctx, void**) -> ACPI_STATUS {
    return (*static_cast<DeviceCallable*>(ctx))(object, level).status_value();
  };

  return acpi::make_status(::AcpiGetDevices(const_cast<char*>(hid), Thunk, &cbk, nullptr));
}

acpi::status<acpi::UniquePtr<ACPI_OBJECT>> AcpiImpl::EvaluateObject(
    ACPI_HANDLE object, const char* pathname, std::optional<std::vector<ACPI_OBJECT>> args) {
  ACPI_OBJECT_LIST params;
  ACPI_OBJECT_LIST* params_ptr = nullptr;
  if (args.has_value()) {
    params.Count = static_cast<UINT32>(args->size());
    params.Pointer = args->data();
    params_ptr = &params;
  }

  ACPI_BUFFER out = {
      .Length = ACPI_ALLOCATE_BUFFER,
      .Pointer = nullptr,
  };

  ACPI_STATUS result = ::AcpiEvaluateObject(object, const_cast<char*>(pathname), params_ptr, &out);
  if (result != AE_OK) {
    return acpi::error(result);
  }
  return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(static_cast<ACPI_OBJECT*>(out.Pointer)));
}

acpi::status<acpi::UniquePtr<ACPI_DEVICE_INFO>> AcpiImpl::GetObjectInfo(ACPI_HANDLE obj) {
  ACPI_DEVICE_INFO* raw = nullptr;
  ACPI_STATUS acpi_status = AcpiGetObjectInfo(obj, &raw);
  UniquePtr<ACPI_DEVICE_INFO> ret{raw};

  if (acpi_status == AE_OK) {
    return acpi::ok(std::move(ret));
  }

  return acpi::error(acpi_status);
}

acpi::status<ACPI_HANDLE> AcpiImpl::GetParent(ACPI_HANDLE child) {
  ACPI_HANDLE out;
  ACPI_STATUS status = AcpiGetParent(child, &out);
  if (status != AE_OK) {
    return acpi::error(status);
  }
  return acpi::ok(out);
}

acpi::status<ACPI_HANDLE> AcpiImpl::GetHandle(ACPI_HANDLE parent, const char* pathname) {
  ACPI_HANDLE out;
  ACPI_STATUS status = AcpiGetHandle(parent, const_cast<char*>(pathname), &out);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  return acpi::ok(out);
}

}  // namespace acpi
