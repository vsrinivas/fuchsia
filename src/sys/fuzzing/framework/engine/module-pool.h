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
#include "src/sys/fuzzing/framework/engine/module-proxy.h"

namespace fuzzing {

// An alias to simplify passing around the shared module pool.
class ModulePool;
using ModulePoolPtr = std::shared_ptr<ModulePool>;

// This class represents a collection of |ModuleProxy| instances.
class ModulePool final {
 public:
  ModulePool() = default;
  ~ModulePool() = default;

  static ModulePoolPtr MakePtr() { return std::make_shared<ModulePool>(); }

  // Returns a pointer to the |ModuleProxy| for a given |id| and |size|, creating it first if
  // necessary.
  ModuleProxy* Get(const std::string& id, size_t size);

  // These correspond to |ModuleProxy| methods, but are applied to all modules.
  size_t Measure();
  size_t Accumulate();
  size_t GetCoverage(size_t* out_num_features);
  void Clear();

 private:
  // Apply a function to all modules.
  void ForEachModule(fit::function<void(ModuleProxy&)> func);

  std::unordered_map<std::string, std::unique_ptr<ModuleProxy>> modules_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ModulePool);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MODULE_POOL_H_
