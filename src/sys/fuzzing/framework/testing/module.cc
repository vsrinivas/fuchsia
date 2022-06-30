// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/module.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeFrameworkModule::FakeFrameworkModule(uint32_t seed) : FakeModule(seed) {
  module_ = std::make_unique<Module>(counters(), pcs(), num_pcs());
}

FakeFrameworkModule::FakeFrameworkModule(std::vector<ModulePC>&& pc_table) noexcept
    : FakeModule(std::move(pc_table)) {
  module_ = std::make_unique<Module>(counters(), pcs(), num_pcs());
}

FakeFrameworkModule& FakeFrameworkModule::operator=(FakeFrameworkModule&& other) noexcept {
  module_ = std::move(other.module_);
  FakeModule::operator=(std::move(other));
  return *this;
}

zx_status_t FakeFrameworkModule::Share(uint64_t target_id, zx::vmo* out) const {
  return module_->Share(target_id, out);
}

}  // namespace fuzzing
