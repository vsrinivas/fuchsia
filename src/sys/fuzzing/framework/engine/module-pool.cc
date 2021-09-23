// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/module-pool.h"

namespace fuzzing {

ModuleProxy* ModulePool::Get(const Identifier& id, size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  Key key = {id, size};
  auto iter = modules_.find(key);
  if (iter == modules_.end()) {
    iter = modules_.emplace(key, std::make_unique<ModuleProxy>(id, size)).first;
  }
  return iter->second.get();
}

void ModulePool::ForEachModule(fit::function<void(ModuleProxy&)> func) {
  std::lock_guard<std::mutex> lock(mutex_);
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

bool ModulePool::Key::operator==(const Key& key) const {
  return id[0] == key.id[0] && id[1] == key.id[1] && size == key.size;
}

size_t ModulePool::Hasher::operator()(const Key& key) const {
  return key.id[0] ^ key.id[1] ^ key.size;
}

}  // namespace fuzzing
