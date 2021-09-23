// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MODULE_POOL_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MODULE_POOL_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stddef.h>

#include <mutex>
#include <unordered_map>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/framework/engine/module-proxy.h"

namespace fuzzing {

// This class represents a collection of |ModuleProxy| instances.
class ModulePool final {
 public:
  ModulePool() = default;
  ~ModulePool() = default;

  // Returns a pointer to the |ModuleProxy| for a given |id| and |size|, creating it first if
  // necessary.
  ModuleProxy* Get(const Identifier& id, size_t size) FXL_LOCKS_EXCLUDED(mutex_);

  // These correspond to |ModuleProxy| methods, but are applied to all modules.
  size_t Measure() FXL_LOCKS_EXCLUDED(mutex_);
  size_t Accumulate() FXL_LOCKS_EXCLUDED(mutex_);
  size_t GetCoverage(size_t* out_num_features) FXL_LOCKS_EXCLUDED(mutex_);
  void Clear() FXL_LOCKS_EXCLUDED(mutex_);

 private:
  // These structures enable convenient mapping below.
  struct Key {
    Identifier id;
    size_t size;
    bool operator==(const Key& key) const;
  };
  struct Hasher {
    size_t operator()(const Key& key) const;
  };

  // Apply a function to all modules.
  void ForEachModule(fit::function<void(ModuleProxy&)> func) FXL_LOCKS_EXCLUDED(mutex_);

  std::mutex mutex_;
  std::unordered_map<Key, std::unique_ptr<ModuleProxy>, Hasher> modules_ FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ModulePool);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MODULE_POOL_H_
