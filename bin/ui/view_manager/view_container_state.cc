// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_container_state.h"

#include <ostream>

#include "lib/fxl/logging.h"

namespace view_manager {

ViewContainerState::ViewContainerState() {}

ViewContainerState::~ViewContainerState() {}

void ViewContainerState::LinkChild(uint32_t key,
                                   std::unique_ptr<ViewStub> child) {
  FXL_DCHECK(children_.find(key) == children_.end());
  FXL_DCHECK(child);
  FXL_DCHECK(!child->is_linked());

  child->SetContainer(this, key);
  children_.emplace(key, std::move(child));
}

std::unique_ptr<ViewStub> ViewContainerState::UnlinkChild(uint32_t key) {
  auto child_it = children_.find(key);
  FXL_DCHECK(child_it != children_.end());
  std::unique_ptr<ViewStub> child(std::move(child_it->second));
  child->Unlink();
  children_.erase(child_it);
  return child;
}

std::vector<std::unique_ptr<ViewStub>> ViewContainerState::UnlinkAllChildren() {
  std::vector<std::unique_ptr<ViewStub>> stubs;
  for (auto& pair : children_) {
    pair.second->Unlink();
    stubs.push_back(std::move(pair.second));
  }
  children_.clear();
  return stubs;
}

ViewState* ViewContainerState::AsViewState() {
  return nullptr;
}

ViewTreeState* ViewContainerState::AsViewTreeState() {
  return nullptr;
}

std::ostream& operator<<(std::ostream& os, ViewContainerState* state) {
  if (!state)
    return os << "null";
  return os << state->FormattedLabel();
}

}  // namespace view_manager
