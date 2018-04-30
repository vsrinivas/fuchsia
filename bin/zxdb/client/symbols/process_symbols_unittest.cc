// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/public/lib/fxl/strings/string_view.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

bool StringEndsWith(fxl::StringView string, fxl::StringView ends_with) {
  if (string.size() < ends_with.size())
    return false;
  return string.substr(string.size() - ends_with.size()) == ends_with;
}

}  // namespace

// This test requires a full Fuchsia build for "ls" to be present.
TEST(ProcessSymbols, Basic) {
  SystemSymbols system;
  std::string system_init_status;
  ASSERT_TRUE(system.Init(&system_init_status)) << system_init_status;

  ProcessSymbols process(&system);

  // Find the build ID of "ls".
  std::string ls_build_id;
  for (const auto& pair : system.build_id_to_file()) {
    if (StringEndsWith(pair.second, "/ls")) {
      ls_build_id = pair.first;
      break;
    }
  }
  ASSERT_FALSE(ls_build_id.empty())
      << "\"ls\" not found in <build_dir>/ids.txt";

  // Notify the process that "ls" has loaded.
  uint64_t ls_base = 0x123561200;
  std::string local_ls_path = process.AddModule(ls_base, ls_build_id, "ls");
  ASSERT_FALSE(local_ls_path.empty());

  // Asking for an address before the beginning should fail.
  Symbol symbol = process.SymbolAtAddress(ls_base - 0x1000);
  EXPECT_FALSE(symbol.valid()) << symbol.file() << " " << symbol.function();

  // Asking for a way big address should fail.
  symbol = process.SymbolAtAddress(ls_base + 0x1000000000000);
  EXPECT_FALSE(symbol.valid()) << symbol.file() << " " << symbol.function();

  // Ask for an address inside "ls", it should resolve to *something*.
  symbol = process.SymbolAtAddress(ls_base + 0x2000);
  EXPECT_TRUE(symbol.valid());
}

}  // namespace zxdb
