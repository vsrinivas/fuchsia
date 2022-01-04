// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/module.h"

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

Identifier FakeFrameworkModule::id() const { return module_->id(); }

Buffer FakeFrameworkModule::Share() { return module_->Share(); }

LlvmModule FakeFrameworkModule::GetLlvmModule() {
  LlvmModule llvm_module;
  llvm_module.set_id(id());
  llvm_module.set_inline_8bit_counters(Share());
  return llvm_module;
}

void FakeFrameworkModule::Update() { module_->Update(); }

void FakeFrameworkModule::Clear() { module_->Clear(); }

}  // namespace fuzzing
