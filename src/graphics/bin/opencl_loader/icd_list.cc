// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/opencl_loader/icd_list.h"

IcdList::IcdList() = default;

void IcdList::Initialize(inspect::Node* parent_node) {
  active_icd_ = parent_node->CreateString("active_icd", "");
}

void IcdList::Add(std::shared_ptr<IcdComponent> component) {
  components_.push_back(std::move(component));
  UpdateCurrentComponent();
}

bool IcdList::UpdateCurrentComponent() {
  for (auto& icd : components_) {
    // Wait for earlier components to start before checking later components.
    if (icd->stage() == IcdComponent::LookupStages::kStarted)
      break;
    if (icd->stage() != IcdComponent::LookupStages::kFinished)
      continue;
    icd->AddManifestToFs();
    active_icd_.Set(icd->child_instance_name());
    // Only one manifest can be exposed at a time.
    return true;
  }
  return false;
}

std::optional<zx::vmo> IcdList::GetVmoMatchingSystemLib(const std::string& library_path) const {
  for (auto& icd : components_) {
    // Wait for earlier components to start before checking later components.
    if (icd->stage() == IcdComponent::LookupStages::kStarted)
      break;
    if (icd->stage() != IcdComponent::LookupStages::kFinished)
      continue;
    if (icd->library_path() != library_path)
      continue;
    // Only ever return clones of the original VMO to clients.  If we handed out
    // the original VMO, even without ZX_RIGHT_WRITE, the client could still
    // modify it using zx_process_write_memory.
    zx::result<zx::vmo> result = icd->CloneVmo();

    if (result.is_ok()) {
      return {std::move(result.value())};
    }
  }
  return {};
}
