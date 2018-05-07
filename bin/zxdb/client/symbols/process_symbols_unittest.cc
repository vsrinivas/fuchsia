// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/module_records.h"
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
  ModuleLoadInfo load_info;
  load_info.base = 0x123561200;
  load_info.build_id = ls_build_id;
  load_info.module_name = "ls";
  std::string local_ls_path = process.AddModule(load_info);
  ASSERT_FALSE(local_ls_path.empty());

  // Asking for an address before the beginning should fail.
  Location loc = process.ResolveAddress(load_info.base - 0x1000);
  EXPECT_EQ(load_info.base - 0x1000, loc.address());
  EXPECT_FALSE(loc.symbol().valid())
      << loc.symbol().file() << " " << loc.symbol().function();

  // Asking for a way big address should fail.
  loc = process.ResolveAddress(load_info.base + 0x1000000000000);
  EXPECT_FALSE(loc.symbol().valid())
      << loc.symbol().file() << " " << loc.symbol().function();

  // Ask for an address inside "ls", it should resolve to *something*.
  loc = process.ResolveAddress(load_info.base + 0x2000);
  EXPECT_TRUE(loc.symbol().valid());
}

}  // namespace zxdb
