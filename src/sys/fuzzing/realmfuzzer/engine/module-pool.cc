// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/module-pool.h"

#include <iomanip>
#include <sstream>

namespace fuzzing {

ModuleProxy* ModulePool::Get(const std::string& id, size_t size) {
  std::ostringstream oss;
  oss << id << std::setw(16) << std::setfill('0') << ":" << size;
  auto key = oss.str();
  auto& module = modules_[key];
  if (!module) {
    module = std::make_unique<ModuleProxy>(id, size);
  }
  return module.get();
}

void ModulePool::ForEachModule(fit::function<void(ModuleProxy&)> func) {
  for (auto& kv : modules_) {
    func(*kv.second);
  }
}

size_t ModulePool::Measure() {
  size_t count = 0;
  ForEachModule([&count](ModuleProxy& module) { count += module.Measure(); });
  return count;
}

size_t ModulePool::Accumulate() {
  size_t count = 0;
  ForEachModule([&count](ModuleProxy& module) { count += module.Accumulate(); });
  return count;
}

size_t ModulePool::GetCoverage(size_t* out_num_features) {
  size_t num_pcs = 0;
  size_t num_features = 0;
  ForEachModule([&num_pcs, &num_features](ModuleProxy& module) {
    size_t tmp_features;
    num_pcs += module.GetCoverage(&tmp_features);
    num_features += tmp_features;
  });
  if (out_num_features) {
    *out_num_features = num_features;
  }
  return num_pcs;
}

void ModulePool::Clear() {
  ForEachModule([](ModuleProxy& module) { module.Clear(); });
}

}  // namespace fuzzing
