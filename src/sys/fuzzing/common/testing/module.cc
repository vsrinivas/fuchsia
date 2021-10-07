// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/module.h"

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
}

FakeModule::FakeModule(std::vector<ModulePC>&& pc_table) noexcept {
  FX_DCHECK(pc_table.size() == kNumPCs);
  counters_.resize(kNumPCs, 0);
  pc_table_ = std::move(pc_table);
}

FakeModule& FakeModule::operator=(FakeModule&& other) noexcept {
  counters_ = std::move(other.counters_);
  pc_table_ = std::move(other.pc_table_);
  return *this;
}

uint8_t& FakeModule::operator[](size_t index) {
  FX_DCHECK(index < num_pcs());
  return counters_[index];
}

void FakeModule::SetCoverage(const Coverage& coverage) {
  memset(counters(), 0, num_pcs());
  for (auto x : coverage) {
    FX_DCHECK(x.first < num_pcs());
    counters_[x.first] = x.second;
  }
}

}  // namespace fuzzing
