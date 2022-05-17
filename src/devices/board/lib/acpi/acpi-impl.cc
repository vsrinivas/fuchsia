// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/acpi-impl.h"

#include <lib/ddk/debug.h>

#include <string>
#include <vector>

#include "src/devices/board/lib/acpi/status.h"

namespace acpi {
namespace {
constexpr uint32_t kAcpiMaxInitTables = 32;
}

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

acpi::status<> AcpiImpl::InstallGpeHandler(ACPI_HANDLE device, uint32_t number, uint32_t type,
                                           GpeHandler handler, void* context) {
  ACPI_STATUS status = AcpiInstallGpeHandler(device, number, type, handler, context);
  return acpi::make_status(status);
}
acpi::status<> AcpiImpl::EnableGpe(ACPI_HANDLE device, uint32_t number) {
  ACPI_STATUS status = AcpiEnableGpe(device, number);
  return acpi::make_status(status);
}

acpi::status<> AcpiImpl::RemoveGpeHandler(ACPI_HANDLE device, uint32_t number, GpeHandler handler) {
  ACPI_STATUS status = AcpiRemoveGpeHandler(device, number, handler);
  return acpi::make_status(status);
}
acpi::status<> AcpiImpl::DisableGpe(ACPI_HANDLE device, uint32_t number) {
  ACPI_STATUS status = AcpiDisableGpe(device, number);
  return acpi::make_status(status);
}

acpi::status<> AcpiImpl::InitializeAcpi() {
  // This sequence is described in section 10.1.2.1 (Full ACPICA Initialization)
  // of the ACPICA developer's reference.
  ACPI_STATUS status = AcpiInitializeSubsystem();
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI: %d", status);
    return acpi::make_status(status);
  }

  status = AcpiInitializeTables(NULL, kAcpiMaxInitTables, FALSE);
  if (status == AE_NOT_FOUND) {
    zxlogf(WARNING, "Could not find ACPI tables");
    return acpi::make_status(status);
  } else if (status == AE_NO_MEMORY) {
    zxlogf(WARNING, "Could not initialize ACPI tables");
    return acpi::make_status(status);
  } else if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI tables for unknown reason");
    return acpi::make_status(status);
  }

  status = AcpiLoadTables();
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not load ACPI tables: %d", status);
    return acpi::make_status(status);
  }

  status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not enable ACPI: %d", status);
    return acpi::make_status(status);
  }

  status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI objects: %d", status);
    return acpi::make_status(status);
  }

  auto apic_status = SetApicIrqMode();
  if (apic_status.status_value() == AE_NOT_FOUND) {
#ifdef __x86_64__
    // Only warn on x86, since this is unlikely to be an issue on ARM.
    zxlogf(WARNING, "Could not find ACPI IRQ mode switch");
#endif
  } else if (apic_status.is_error()) {
    zxlogf(WARNING, "Failed to set APIC IRQ mode: %d", apic_status.status_value());
    return apic_status.take_error();
  }

  // We need to tell ACPICA about all the wake GPEs, but
  // if it fails for some reason we don't want to block booting the system.
  auto wake_status = DiscoverWakeGpes();
  if (wake_status.is_error()) {
    zxlogf(WARNING, "Failed to discover wake GPEs: %d", wake_status.status_value());
  }
  status = AcpiUpdateAllGpes();
  if (status != AE_OK) {
    zxlogf(WARNING, "Could not initialize ACPI GPEs: %d", status);
    return acpi::make_status(status);
  }

  return acpi::ok();
}

acpi::status<> AcpiImpl::SetupGpeForWake(ACPI_HANDLE wake_dev, ACPI_HANDLE gpe_dev,
                                         uint32_t gpe_num) {
  return acpi::make_status(AcpiSetupGpeForWake(wake_dev, gpe_dev, gpe_num));
}

}  // namespace acpi
