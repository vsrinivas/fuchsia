// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "garnet/bin/zxdb/client/symbols/module_symbols.h"
#include "garnet/bin/zxdb/client/symbols/test_symbol_module.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class ScopedUnlink {
 public:
  explicit ScopedUnlink(const char* name) : name_(name) {}
  ~ScopedUnlink() { EXPECT_EQ(0, unlink(name_)); }

 private:
  const char* name_;
};

}  // namespace

// Trying to load a nonexistand file should error.
TEST(ModuleSymbols, NonExistantFile) {
  ModuleSymbols module(TestSymbolModule::GetTestFileName() + "_NONEXISTANT");
  Err err = module.Load();
  EXPECT_TRUE(err.has_error());
}

// Trying to load a random file should error.
TEST(ModuleSymbols, BadFileType) {
  char temp_name[] = "/tmp/zxdb_symbol_test.txtXXXXXX";
  int fd = mkstemp(temp_name);
  ASSERT_LT(0, fd) << "Could not create temporary file: " << temp_name;

  // Just use the file name itself as the contents of the file.
  ScopedUnlink unlink(temp_name);
  EXPECT_LT(0, write(fd, temp_name, strlen(temp_name)));
  close(fd);

  ModuleSymbols module(TestSymbolModule::GetTestFileName() + "_NONEXISTANT");
  Err err = module.Load();
  EXPECT_TRUE(err.has_error());
}

TEST(ModuleSymbols, Basic) {
  ModuleSymbols module(TestSymbolModule::GetTestFileName());
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // MyFunction() should have one implementation.
  std::vector<uint64_t> addrs =
      module.AddressesForFunction(TestSymbolModule::kMyFunctionName);
  ASSERT_EQ(1u, addrs.size());

  // That address should resolve back to the function name.
  Location loc = module.LocationForAddress(addrs[0]);
  EXPECT_TRUE(loc.is_symbolized());
  EXPECT_EQ("zxdb_symbol_test.cc", loc.file_line().GetFileNamePart());
  EXPECT_EQ(TestSymbolModule::kMyFunctionLine, loc.file_line().line());
}

}  // namespace zxdb
