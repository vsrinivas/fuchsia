// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_MODULE_H_
#define SRC_SYS_FUZZING_COMMON_MODULE_H_

#include <stdint.h>

namespace fuzzing {

// The array presented by |__sanititizer_cov_pcs_init| is actually a table of PCs and flags.
struct ModulePC {
  uintptr_t pc;
  uintptr_t flags;

  ModulePC() = default;
  ModulePC(uintptr_t pc_, uintptr_t flags_) : pc(pc_), flags(flags_) {}
  ~ModulePC() = default;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_MODULE_H_
