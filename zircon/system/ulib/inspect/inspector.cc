// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/result.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/heap.h>
#include <lib/inspect/cpp/vmo/state.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <sstream>

using inspect::internal::Heap;
using inspect::internal::State;

namespace inspect {

namespace {
const InspectSettings kDefaultInspectSettings = {.maximum_size = 256 * 1024};
}  // namespace

Inspector::Inspector() : Inspector(kDefaultInspectSettings) {}

Inspector::Inspector(const InspectSettings& settings)
    : root_(std::make_shared<Node>()), value_list_(std::make_shared<ValueList>()) {
  if (settings.maximum_size == 0) {
    return;
  }

  state_ = State::CreateWithSize(settings.maximum_size);
  if (!state_) {
    return;
  }

  *root_ = state_->CreateRootNode();
}

Inspector::Inspector(zx::vmo vmo)
    : root_(std::make_shared<Node>()), value_list_(std::make_shared<ValueList>()) {
  size_t size;

  zx_status_t status;
  if (ZX_OK != (status = vmo.get_size(&size))) {
    return;
  }

  if (size == 0) {
    // VMO cannot be zero size.
    return;
  }

  // Decommit all pages, reducing memory usage of the VMO and zeroing it.
  if (ZX_OK != (status = vmo.op_range(ZX_VMO_OP_DECOMMIT, 0, size, nullptr, 0))) {
    return;
  }

  state_ = State::Create(std::make_unique<Heap>(std::move(vmo)));
  if (!state_) {
    return;
  }

  *root_ = state_->CreateRootNode();
}

zx::vmo Inspector::DuplicateVmo() const {
  zx::vmo ret;

  if (state_) {
    state_->DuplicateVmo(&ret);
  }

  return ret;
}

zx::vmo Inspector::CopyVmo() const {
  zx::vmo ret;

  state_->Copy(&ret);

  return ret;
}

std::vector<uint8_t> Inspector::CopyBytes() const {
  std::vector<uint8_t> ret;
  state_->CopyBytes(&ret);
  return ret;
}

Node& Inspector::GetRoot() const { return *root_; }

std::vector<std::string> Inspector::GetChildNames() const { return state_->GetLinkNames(); }

fit::promise<Inspector> Inspector::OpenChild(const std::string& child_name) const {
  return state_->CallLinkCallback(child_name);
}

namespace internal {
std::shared_ptr<State> GetState(const Inspector* inspector) { return inspector->state_; }
}  // namespace internal

}  // namespace inspect
