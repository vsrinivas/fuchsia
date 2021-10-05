// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/module.h"

#include <lib/syslog/cpp/macros.h>

#include <random>

namespace fuzzing {

FakeModule::FakeModule(uint32_t seed) {
  counters_.resize(kNumPCs, 0);
  pc_table_.reserve(kNumPCs);
  std::minstd_rand prng(seed);
  uintptr_t pc = prng();
  for (size_t i = 0; i < kNumPCs; ++i) {
    pc += prng() % 512;
    pc_table_.emplace_back(pc, (prng() % 8) == 0);
  }
  module_ = std::make_unique<Module>(counters_.data(),
                                     reinterpret_cast<const uintptr_t*>(pc_table_.data()), kNumPCs);
}

FakeModule::FakeModule(std::vector<Module::PC>&& pc_table) {
  FX_DCHECK(pc_table.size() == kNumPCs);
  counters_.resize(kNumPCs, 0);
  pc_table_ = std::move(pc_table);
  module_ = std::make_unique<Module>(counters_.data(),
                                     reinterpret_cast<const uintptr_t*>(pc_table_.data()), kNumPCs);
}

FakeModule& FakeModule::operator=(FakeModule&& other) noexcept {
  module_ = std::move(other.module_);
  counters_ = std::move(other.counters_);
  pc_table_ = std::move(other.pc_table_);
  return *this;
}

uint8_t& FakeModule::operator[](size_t index) {
  FX_DCHECK(index < num_pcs());
  return counters_[index];
}

Identifier FakeModule::id() const { return module_->id(); }

void FakeModule::SetCoverage(const Coverage& coverage) {
  memset(counters(), 0, num_pcs());
  for (auto x : coverage) {
    FX_DCHECK(x.first < num_pcs());
    counters_[x.first] = x.second;
  }
}

Buffer FakeModule::Share() { return module_->Share(); }

void FakeModule::Update() { module_->Update(); }

void FakeModule::Clear() { module_->Clear(); }

}  // namespace fuzzing
