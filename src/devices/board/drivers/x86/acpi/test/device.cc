// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/test/device.h"

#include <lib/ddk/debug.h>

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

  for (auto& child : children_) {
    if (child->name_ == segment) {
      return child->FindByPathInternal(leftover);
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
}  // namespace acpi::test
