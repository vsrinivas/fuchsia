// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "garnet/bin/zxdb/symbols/loaded_module_symbols.h"
#include "gtest/gtest.h"

namespace zxdb {

// Trying to load a random file should error.
TEST(LoadedModuleSymbols, ElfLookup) {
  LoadedModuleSymbols mod(nullptr, "bad1deaf00dbabe", 0);

  std::vector<debug_ipc::ElfSymbol> syms;

  syms.emplace_back(debug_ipc::ElfSymbol{.name = "testy", .value = 720});

  mod.SetElfSymbols(syms);

  InputLocation elf_loc("testy", true);
  InputLocation normal_loc("testy");

  auto elf_resolve = mod.ResolveInputLocation(elf_loc);
  auto normal_resolve = mod.ResolveInputLocation(normal_loc);

  ASSERT_EQ(1u, elf_resolve.size());
  ASSERT_EQ(1u, normal_resolve.size());

  EXPECT_EQ(720u, elf_resolve[0].address());
  EXPECT_EQ(720u, normal_resolve[0].address());
}

}  // namespace zxdb
