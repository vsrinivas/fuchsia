// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TESTING_MODULE_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TESTING_MODULE_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/testing/module.h"
#include "src/sys/fuzzing/framework/target/module.h"

namespace fuzzing {

using ::fuchsia::fuzzer::LlvmModule;

// Wraps a |Module| and automatically provides fake counters and PC tables based on a seed value.
class FakeFrameworkModule final : public FakeModule {
 public:
  // Make a fake module with randomized PCs.
  explicit FakeFrameworkModule(uint32_t seed = 1);

  // Make a fake module with the given PCs.
  explicit FakeFrameworkModule(std::vector<ModulePC>&& pc_table) noexcept;

  FakeFrameworkModule(FakeFrameworkModule&& other) { *this = std::move(other); }
  ~FakeFrameworkModule() override = default;

  FakeFrameworkModule& operator=(FakeFrameworkModule&& other) noexcept;

  // See corresponding |Module| methods.
  Identifier id() const;
  Buffer Share();
  LlvmModule GetLlvmModule();
  void Update();
  void Clear();

 private:
  std::unique_ptr<Module> module_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeFrameworkModule);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_MODULE_H_
