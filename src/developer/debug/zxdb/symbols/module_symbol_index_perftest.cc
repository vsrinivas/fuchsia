// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/module_symbol_index.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/perf_test.h"

namespace zxdb {

TEST(ModuleLoad, Perf) {
  PerfTimeLogger logger("zxdb", "ModuleLoad");
  // TODO(brettw) write this.
}

}  // namespace zxdb
