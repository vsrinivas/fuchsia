// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/acpi-impl.h"

#include <string>
#include <vector>

#include "src/devices/board/lib/acpi/status.h"

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

acpi::status<acpi::UniquePtr<ACPI_RESOURCE>> AcpiImpl::BufferToResource(
    cpp20::span<uint8_t> buffer) {
  ACPI_RESOURCE* res;
  if (buffer.size() > std::numeric_limits<uint16_t>::max()) {
    return acpi::error(AE_BAD_VALUE);
  }
  ACPI_STATUS status =
      AcpiBufferToResource(buffer.data(), static_cast<uint16_t>(buffer.size()), &res);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  return acpi::ok(acpi::UniquePtr<ACPI_RESOURCE>(res));
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

acpi::status<std::string> AcpiImpl::GetPath(ACPI_HANDLE object) {
  AcpiBuffer<char> out;
  ACPI_STATUS status = AcpiGetName(object, ACPI_FULL_PATHNAME, &out);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  std::string ret(static_cast<char*>(out.Pointer));
  return acpi::ok(std::move(ret));
}

acpi::status<> AcpiImpl::InstallNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                              NotifyHandlerCallable callable, void* context) {
  ACPI_STATUS status = AcpiInstallNotifyHandler(object, mode, callable, context);
  return acpi::make_status(status);
}

acpi::status<> AcpiImpl::RemoveNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                             NotifyHandlerCallable callable) {
  ACPI_STATUS status = AcpiRemoveNotifyHandler(object, mode, callable);
  return acpi::make_status(status);
}

acpi::status<uint32_t> AcpiImpl::AcquireGlobalLock(uint16_t timeout) {
  uint32_t handle;
  ACPI_STATUS status = AcpiAcquireGlobalLock(timeout, &handle);
  if (status != AE_OK) {
    return acpi::error(status);
  }

  return acpi::ok(handle);
}

acpi::status<> AcpiImpl::ReleaseGlobalLock(uint32_t handle) {
  ACPI_STATUS status = AcpiReleaseGlobalLock(handle);
  return acpi::make_status(status);
}

acpi::status<> AcpiImpl::InstallAddressSpaceHandler(ACPI_HANDLE object,
                                                    ACPI_ADR_SPACE_TYPE space_id,
                                                    AddressSpaceHandler handler,
                                                    AddressSpaceSetup setup, void* context) {
  ACPI_STATUS status = AcpiInstallAddressSpaceHandler(object, space_id, handler, setup, context);
  return acpi::make_status(status);
}

acpi::status<> AcpiImpl::RemoveAddressSpaceHandler(ACPI_HANDLE object, ACPI_ADR_SPACE_TYPE space_id,
                                                   AddressSpaceHandler handler) {
  ACPI_STATUS status = AcpiRemoveAddressSpaceHandler(object, space_id, handler);
  return acpi::make_status(status);
}
}  // namespace acpi
