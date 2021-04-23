// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/icd_list.h"

IcdList::IcdList() = default;

std::optional<zx::vmo> IcdList::GetVmoMatchingSystemLib(const std::string& library_path) const {
  for (auto& icd : components_) {
    // Wait for earlier components to start before checking later components.
    if (icd->stage() == IcdComponent::LookupStages::kStarted)
      break;
    if (icd->stage() != IcdComponent::LookupStages::kFinished)
      continue;
    if (icd->library_path() != library_path)
      continue;
    zx::status<zx::vmo> result = icd->DuplicateVmo();

    if (result.is_ok()) {
      return {std::move(result.value())};
    }
  }
  return {};
}
