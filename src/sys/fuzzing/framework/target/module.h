// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TARGET_MODULE_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TARGET_MODULE_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/shared-memory.h"

namespace fuzzing {

using Identifier = std::array<uint64_t, 2>;

// Represents an LLVM "module", e.g. a collection of translation units, such as a shared object
// library. The instrumented processes rely on instances of these classes to collect feedback (e.g.
// code coverage) for the fuzzer engine.
class Module final {
 public:
  // The array presented by |__sanititizer_cov_pcs_init| is actually a table of PCs and flags.
  struct PC {
    uintptr_t pc;
    uintptr_t flags;
  };

  // Returns a unique, position-independent identifier for the module.
  static Identifier Identify(const uintptr_t* pcs, size_t num_pcs);

  // For every edge, there should be an 8-bit counter, a PC uintptr_t, and a PCFlags uintptr_t.
  // Thus, |counters| should be an array of length |num_pcs|, and |pcs| of length |num_pcs| * 2.
  // See also: https://clang.llvm.org/docs/SanitizerCoverage.html
  Module(uint8_t* counters, const uintptr_t* pcs, size_t num_pcs);
  Module(Module&& other) noexcept { *this = std::move(other); }
  ~Module() = default;
  Module& operator=(Module&& other) noexcept;

  // Return a unique identifier for this module as described in |fuchsia.fuzzer.Feedback|. This
  // identifier will be the same for the same module across multiple processes and/or invocations.
  const Identifier& id() const { return id_; }

  // Shares the VMO containing the code coverage.
  Buffer Share() { return counters_.Share(); }

  // Update the code-coverage counters to produce feedback for this module.
  void Update() { counters_.Update(); }

  // Reset the code-coverage counters for this module.
  void Clear() { counters_.Clear(); }

 private:
  Identifier id_;
  SharedMemory counters_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Module);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TARGET_MODULE_H_
