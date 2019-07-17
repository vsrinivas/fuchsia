// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <memory>
#include <sstream>

#include <lib/fit/result.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/heap.h>

namespace inspect {

namespace {
const InspectSettings kDefaultInspectSettings = {.maximum_size = 256 * 1024};
}  // namespace

Inspector::Inspector(const std::string& name) : Inspector(name, kDefaultInspectSettings) {}

Inspector::Inspector(const std::string& name, const InspectSettings& settings)
    : root_(std::make_unique<Node>()) {
  zx::vmo vmo;
  if (settings.maximum_size == 0 || ZX_OK != zx::vmo::create(settings.maximum_size, 0, &vmo)) {
    return;
  }

  auto heap = std::make_unique<Heap>(std::move(vmo));
  state_ = State::Create(std::move(heap));
  if (!state_) {
    return;
  }

  *root_ = state_->CreateNode(name, 0 /* parent */);
}

fit::result<const zx::vmo*> Inspector::GetVmo() const {
  if (!state_) {
    return fit::error();
  }

  return fit::ok(&state_->GetVmo());
}

Node& Inspector::GetRoot() const { return *root_; }

Node Inspector::TakeRoot() {
  auto tmp = std::make_unique<Node>();
  root_.swap(tmp);
  return std::move(*tmp);
}

std::string UniqueName(const std::string& prefix) {
  static std::atomic_uint_fast64_t next_id;
  std::ostringstream out;
  auto value = next_id.fetch_add(1);
  out << prefix << "0x" << std::hex << value;
  return out.str();
}

}  // namespace inspect
