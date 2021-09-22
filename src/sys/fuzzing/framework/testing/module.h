// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TESTING_MODULE_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TESTING_MODULE_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/framework/target/module.h"

namespace fuzzing {

using Coverage = std::vector<std::pair<size_t, uint8_t>>;

// Wraps a |Module| and automatically provides fake counters and PC tables based on a seed value.
class FakeModule final {
 public:
  static constexpr size_t kNumPCs = 256;

  // Make a fake module with randomized PCs.
  explicit FakeModule(uint32_t seed = 1);

  // Make a fake module with the given PCs.
  explicit FakeModule(std::vector<Module::PC>&& pc_table);

  FakeModule(FakeModule&& other) { *this = std::move(other); }
  ~FakeModule() = default;

  FakeModule& operator=(FakeModule&& other) noexcept;

  // Returns a reference to a counter location. |index| must be less than |kNumPCs|.
  uint8_t& operator[](size_t index);

  Identifier id() const;

  size_t num_pcs() const { return counters_.size(); }

  const uint8_t* counters() const { return counters_.data(); }
  uint8_t* counters() { return counters_.data(); }

  const uint8_t* counters_end() const { return counters() + num_pcs(); }
  uint8_t* counters_end() { return counters() + num_pcs(); }

  const uintptr_t* pcs() const { return reinterpret_cast<const uintptr_t*>(pc_table()); }

  const uintptr_t* pcs_end() const { return reinterpret_cast<const uintptr_t*>(pc_table_end()); }

  const Module::PC* pc_table() const { return pc_table_.data(); }

  const Module::PC* pc_table_end() const { return pc_table() + num_pcs(); }

  // Sets the inline, 8-bit code coverage counters.
  void SetCoverage(const Coverage& coverage);

  // Methods for sharing counters, e.g. via |AddFeedback|.
  Buffer Share();
  void Update();
  void Clear();

 private:
  std::unique_ptr<Module> module_;
  std::vector<uint8_t> counters_;
  std::vector<Module::PC> pc_table_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeModule);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_MODULE_H_
