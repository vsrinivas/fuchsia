// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <vector>

namespace acpi {

acpi::status<uint8_t> Acpi::CallBbn(ACPI_HANDLE obj) {
  auto ret = EvaluateObject(obj, "_BBN", std::nullopt);
  if (ret.is_error()) {
    return ret.take_error();
  }

  if (ret->Type != ACPI_TYPE_INTEGER) {
    return acpi::error(AE_TYPE);
  }

  return acpi::ok(ret->Integer.Value);
}

acpi::status<uint16_t> Acpi::CallSeg(ACPI_HANDLE obj) {
  auto ret = EvaluateObject(obj, "SEG", std::nullopt);

  if (ret.is_error()) {
    return ret.take_error();
  }

  if (ret->Type != ACPI_TYPE_INTEGER) {
    return acpi::error(AE_TYPE);
  }
  // Lower 8 bits of _SEG returned integer is the PCI segment group.
  return acpi::ok(ret->Integer.Value & 0xff);
}

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

acpi::status<acpi::UniquePtr<ACPI_OBJECT>> RealAcpi::EvaluateObject(
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

acpi::status<acpi::UniquePtr<ACPI_DEVICE_INFO>> RealAcpi::GetObjectInfo(ACPI_HANDLE obj) {
  ACPI_DEVICE_INFO* raw = nullptr;
  ACPI_STATUS acpi_status = AcpiGetObjectInfo(obj, &raw);
  UniquePtr<ACPI_DEVICE_INFO> ret{raw};

  if (acpi_status == AE_OK) {
    return acpi::ok(std::move(ret));
  }

  return acpi::error(acpi_status);
}

acpi::status<ACPI_HANDLE> RealAcpi::GetParent(ACPI_HANDLE child) {
  ACPI_HANDLE out;
  ACPI_STATUS status = AcpiGetParent(child, &out);
  if (status != AE_OK) {
    return acpi::error(status);
  }
  return acpi::ok(out);
}

acpi::status<ACPI_HANDLE> RealAcpi::GetHandle(ACPI_HANDLE parent, const char* pathname) {
  ACPI_HANDLE out;
  ACPI_STATUS status = AcpiGetHandle(parent, const_cast<char*>(pathname), &out);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  return acpi::ok(out);
}

}  // namespace acpi
