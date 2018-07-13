// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
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
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName() +
                           "_NONEXISTANT", "");
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

  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName() +
                           "_NONEXISTANT", "");
  Err err = module.Load();
  EXPECT_TRUE(err.has_error());
}

TEST(ModuleSymbols, Basic) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // MyFunction() should have one implementation.
  std::vector<uint64_t> addrs =
      module.RelativeAddressesForFunction(TestSymbolModule::kMyFunctionName);
  ASSERT_EQ(1u, addrs.size());

  // That address should resolve back to the function name.
  Location loc = module.RelativeLocationForRelativeAddress(addrs[0]);
  EXPECT_TRUE(loc.is_symbolized());
  EXPECT_EQ("zxdb_symbol_test.cc", loc.file_line().GetFileNamePart());
  EXPECT_EQ(TestSymbolModule::kMyFunctionLine, loc.file_line().line());
}

TEST(ModuleSymbols, LineDetailsForRelativeAddress) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Get the canonical file name to test.
  auto file_matches = module.FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  // Get address of line 28 which is a normal line with code on both sides.
  int const kLineToQuery = 28;
  std::vector<uint64_t> addrs;
  addrs = module.RelativeAddressesForLine(FileLine(file_name, kLineToQuery));
  ASSERT_LE(1u, addrs.size());
  Location location = module.RelativeLocationForRelativeAddress(addrs[0]);
  EXPECT_EQ(kLineToQuery, location.file_line().line());
  EXPECT_EQ(file_name, location.file_line().file());

  // Lookup the line info. Normally we expect one line table entry for this but
  // don't want to assume that since the compiler could emit multiple entries
  // for it.
  LineDetails line_details = module.LineDetailsForRelativeAddress(addrs[0]);
  EXPECT_EQ(file_name, line_details.file_line().file());
  EXPECT_EQ(kLineToQuery, line_details.file_line().line());
  ASSERT_FALSE(line_details.entries().empty());
  uint64_t begin_range = line_details.entries().front().range.begin();
  uint64_t end_range = line_details.entries().back().range.end();
  EXPECT_LT(begin_range, end_range);

  // The address before the beginning of the range should be the previous line.
  LineDetails prev_details =
      module.LineDetailsForRelativeAddress(begin_range - 1);
  EXPECT_EQ(kLineToQuery - 1, prev_details.file_line().line());
  EXPECT_EQ(file_name, prev_details.file_line().file());
  ASSERT_FALSE(prev_details.entries().empty());
  EXPECT_EQ(begin_range, prev_details.entries().back().range.end());

  // The end of the range (which is non-inclusive) should be the next line.
  LineDetails next_details = module.LineDetailsForRelativeAddress(end_range);
  EXPECT_EQ(kLineToQuery + 1, next_details.file_line().line());
  EXPECT_EQ(file_name, next_details.file_line().file());
  ASSERT_FALSE(next_details.entries().empty());
  EXPECT_EQ(end_range, next_details.entries().front().range.begin());
}

TEST(ModuleSymbols, AddressesForLine) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Get the canonical file name to test.
  auto file_matches = module.FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  // Basic one, look for line 27 which is a normal statement.
  std::vector<uint64_t> addrs;
  addrs = module.RelativeAddressesForLine(FileLine(file_name, 27));
  ASSERT_LE(1u, addrs.size());
  Location location = module.RelativeLocationForRelativeAddress(addrs[0]);
  EXPECT_EQ(27, location.file_line().line());
  EXPECT_EQ(file_name, location.file_line().file());

  // Line 26 is a comment line, looking it up should get the following line.
  addrs = module.RelativeAddressesForLine(FileLine(file_name, 26));
  ASSERT_LE(1u, addrs.size());
  location = module.RelativeLocationForRelativeAddress(addrs[0]);
  EXPECT_EQ(27, location.file_line().line());
  EXPECT_EQ(file_name, location.file_line().file());

  // Line 15 is the beginning of the templatized function. There should be
  // two matches since its instantiated twice.
  addrs = module.RelativeAddressesForLine(FileLine(file_name, 15));
  ASSERT_EQ(2u, addrs.size());
  location = module.RelativeLocationForRelativeAddress(addrs[0]);
  EXPECT_EQ(15, location.file_line().line());
  EXPECT_EQ(file_name, location.file_line().file());
  location = module.RelativeLocationForRelativeAddress(addrs[1]);
  EXPECT_EQ(15, location.file_line().line());
  EXPECT_EQ(file_name, location.file_line().file());

  // Line 17 is only present in one of the two template instantiations.
  // We should only find it once (see note below about case #2).
  addrs = module.RelativeAddressesForLine(FileLine(file_name, 17));
  ASSERT_TRUE(addrs.size() == 1u || addrs.size() == 2u);
  location = module.RelativeLocationForRelativeAddress(addrs[0]);
  EXPECT_EQ(17, location.file_line().line());
  if (addrs.size() == 2u) {
    // MSVC in debug mode will emit the full code in both instantiations of the
    // template which is valid. To be more robust this test allows that form
    // even though Clang doesn't do this. The important thing is that looking
    // up line 17 never gives us line 19 (which is the other template
    // instantiation).
    location = module.RelativeLocationForRelativeAddress(addrs[1]);
    EXPECT_EQ(17, location.file_line().line());
  }
}

}  // namespace zxdb
