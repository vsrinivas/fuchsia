// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/test/mock-acpi.h"

namespace acpi::test {
acpi::status<> MockAcpi::WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object,
                                       uint32_t max_depth, NamespaceCallable cbk) {
  Device* device;
  if (start_object == ACPI_ROOT_OBJECT) {
    device = root_.get();
  } else {
    device = static_cast<Device*>(start_object);
  }

  return WalkNamespaceInternal(type, device, max_depth, 0, cbk);
}

acpi::status<> MockAcpi::WalkNamespaceInternal(ACPI_OBJECT_TYPE type, Device* start_object,
                                               uint32_t max_depth, uint32_t cur_depth,
                                               NamespaceCallable& cbk) {
  if (type != ACPI_TYPE_DEVICE) {
    return acpi::error(AE_NOT_IMPLEMENTED);
  }

  if (max_depth == 0) {
    return acpi::ok();
  }

  for (auto& child : start_object->children()) {
    auto ret = cbk(child.get(), cur_depth, WalkDirection::Descending);
    if (ret.is_error()) {
      return ret.take_error();
    }

    ret = WalkNamespaceInternal(type, child.get(), max_depth - 1, cur_depth + 1, cbk);
    if (ret.is_error()) {
      return ret.take_error();
    }

    ret = cbk(child.get(), cur_depth, WalkDirection::Ascending);
    if (ret.is_error()) {
      return ret.take_error();
    }
  }

  return acpi::ok();
}

acpi::status<> MockAcpi::WalkResources(ACPI_HANDLE object, const char* resource_name,
                                       ResourcesCallable cbk) {
  if (strcmp(resource_name, "_CRS") != 0) {
    return acpi::error(AE_NOT_FOUND);
  }

  Device* d = ToDevice(object);
  if (!d) {
    return acpi::error(AE_BAD_PARAMETER);
  }
  ZX_ASSERT_MSG((d->sta() & ACPI_STA_DEVICE_ENABLED) == ACPI_STA_DEVICE_ENABLED,
                "Attempted to access resources on a device that isn't enabled");
  if (d->resources().empty()) {
    return acpi::error(AE_NOT_FOUND);
  }

  for (ACPI_RESOURCE resource : d->resources()) {
    auto ret = cbk(&resource);
    if (ret.is_error()) {
      return ret.take_error();
    }
  }

  return acpi::ok();
}

acpi::status<acpi::UniquePtr<ACPI_DEVICE_INFO>> MockAcpi::GetObjectInfo(ACPI_HANDLE obj) {
  Device* d = ToDevice(obj);
  size_t cid_entries = d->cids().size();
  ACPI_DEVICE_INFO* info = (ACPI_DEVICE_INFO*)ACPI_ALLOCATE_ZEROED(
      sizeof(*info) + (sizeof(ACPI_PNP_DEVICE_ID) * cid_entries));
  info->InfoSize = sizeof(*info);
  info->Type = ACPI_TYPE_DEVICE;
  info->Name = d->fourcc_name();

  uint16_t valid = 0;

  if (d->adr().has_value()) {
    valid |= ACPI_VALID_ADR;
    info->Address = d->adr().value();
  }

  if (d->hid().has_value()) {
    valid |= ACPI_VALID_HID;
    const std::string& value = d->hid().value();
    info->HardwareId.Length = value.size();
    info->HardwareId.String = const_cast<char*>(value.data());
    if (value == kPciPnpID || value == kPciePnpID) {
      info->Flags |= ACPI_PCI_ROOT_BRIDGE;
    }
  }

  if (!d->cids().empty()) {
    valid |= ACPI_VALID_CID;
    info->CompatibleIdList.ListSize = d->cids().size() * sizeof(ACPI_PNP_DEVICE_ID);
    info->CompatibleIdList.Count = d->cids().size();
    for (size_t i = 0; i < d->cids().size(); i++) {
      const std::string& cid = d->cids()[i];
      info->CompatibleIdList.Ids[i].Length = cid.size();
      info->CompatibleIdList.Ids[i].String = const_cast<char*>(cid.data());
      if (cid == kPciPnpID || cid == kPciePnpID) {
        info->Flags |= ACPI_PCI_ROOT_BRIDGE;
      }
    }
  }

  info->Valid = valid;
  return acpi::ok(acpi::UniquePtr<ACPI_DEVICE_INFO>(info));
}

acpi::status<ACPI_HANDLE> MockAcpi::GetHandle(ACPI_HANDLE parent, const char* pathname) {
  Device* start = ToDevice(parent);
  if (start == nullptr) {
    start = root_.get();
  }
  Device* ret = start->FindByPath(pathname);
  if (ret == nullptr) {
    return acpi::error(AE_NOT_FOUND);
  }
  return acpi::ok(ret);
}

acpi::status<acpi::UniquePtr<ACPI_OBJECT>> MockAcpi::EvaluateObject(
    ACPI_HANDLE object, const char* pathname, std::optional<std::vector<ACPI_OBJECT>> args) {
  if (pathname[0] == '^' || pathname[0] == '\\') {
    return acpi::error(AE_NOT_IMPLEMENTED);
  }
  if (object == nullptr) {
    object = ACPI_ROOT_OBJECT;
  }
  Device* device = ToDevice(object);

  return device->EvaluateObject(pathname, std::move(args));
}

acpi::status<std::string> MockAcpi::GetPath(ACPI_HANDLE object) {
  Device* device = ToDevice(object);
  return acpi::ok(device->GetAbsolutePath());
}

acpi::status<> MockAcpi::InstallNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                              NotifyHandlerCallable callable, void* context) {
  // The root device has a special behaviour, where it should receive all notifications.
  // We don't use this behaviour, so don't implement it.
  ZX_ASSERT_MSG(object != ACPI_ROOT_OBJECT, "Root object notifications are unimplemented");
  Device* device = ToDevice(object);

  return device->InstallNotifyHandler(callable, context, mode);
}

acpi::status<> MockAcpi::RemoveNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                             NotifyHandlerCallable callable) {
  Device* device = ToDevice(object);
  return device->RemoveNotifyHandler(callable, mode);
}

}  // namespace acpi::test
