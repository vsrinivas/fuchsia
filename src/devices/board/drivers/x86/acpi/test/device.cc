// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/test/device.h"

#include <lib/ddk/debug.h>

#include "src/devices/board/drivers/x86/acpi/status.h"
#include "src/devices/board/drivers/x86/acpi/util.h"
#include "src/devices/lib/acpi/util.h"

namespace acpi::test {

Device* Device::FindByPath(std::string path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path[0] == '\\') {
    Device* root = this;
    while (root->parent_) {
      root = root->parent_;
    }
    return root->FindByPathInternal(path.substr(1));
  }
  if (path[0] == '^') {
    if (parent_) {
      return parent_->FindByPathInternal(path.substr(1));
    }
    return nullptr;
  }
  return FindByPathInternal(path);
}

Device* Device::FindByPathInternal(std::string path) {
  if (path.empty()) {
    return this;
  }
  std::string_view segment;
  std::string leftover;
  auto pos = path.find('.');
  if (pos == std::string::npos) {
    segment = path;
    leftover = "";
  } else {
    segment = std::string_view(path.data(), pos);
    leftover = path.substr(pos + 1);
  }

  if (Device* child = LookupChild(segment)) {
    return child->FindByPathInternal(leftover);
  }
  return nullptr;
}

Device* Device::LookupChild(std::string_view name) {
  for (auto& child : children_) {
    if (child->name_ == name) {
      return child.get();
    }
  }
  return nullptr;
}

std::string Device::GetAbsolutePath() {
  std::string ret = name_;
  Device* cur = parent_;
  while (cur) {
    if (cur->parent_) {
      // If we have a parent, then separate names by '.'.
      ret = cur->name_ + "." + ret;
    } else {
      // The root node is called '\' and doesn't need a separator.
      ret = cur->name_ + ret;
    }
    cur = cur->parent_;
  }
  return ret;
}

acpi::status<acpi::UniquePtr<ACPI_OBJECT>> Device::EvaluateObject(
    std::string pathname, std::optional<std::vector<ACPI_OBJECT>> args) {
  auto pos = pathname.find('.');
  if (pos != std::string::npos) {
    Device* d = LookupChild(std::string_view(pathname.data(), pos));
    if (d == nullptr) {
      return acpi::error(AE_NOT_FOUND);
    }
    return d->EvaluateObject(pathname.substr(pos + 1), std::move(args));
  }

  if (pathname == "_DSD") {
    // Number of objects we need to create: one for each UUID, one for each set of values.
    size_t object_count = dsd_.size() * 2;
    // One for the top-level "package".
    object_count += 1;

    ACPI_OBJECT* array =
        static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(ACPI_OBJECT) * object_count));
    acpi::UniquePtr<ACPI_OBJECT> objects(array);

    array[0] = ACPI_OBJECT{.Package = {
                               .Type = ACPI_TYPE_PACKAGE,
                               .Count = static_cast<uint32_t>(object_count - 1),
                               .Elements = &array[1],
                           }};
    size_t i = 1;
    for (auto& pair : dsd_) {
      array[i] = ACPI_OBJECT{.Buffer = {
                                 .Type = ACPI_TYPE_BUFFER,
                                 .Length = acpi::kUuidBytes,
                                 .Pointer = const_cast<uint8_t*>(pair.first.bytes),
                             }};
      i++;

      array[i] = ACPI_OBJECT{.Package = {
                                 .Type = ACPI_TYPE_PACKAGE,
                                 .Count = static_cast<uint32_t>(pair.second.size()),
                                 .Elements = pair.second.data(),
                             }};
      i++;
    }

    return acpi::ok(std::move(objects));
  }
  return acpi::error(AE_NOT_FOUND);
}
}  // namespace acpi::test
