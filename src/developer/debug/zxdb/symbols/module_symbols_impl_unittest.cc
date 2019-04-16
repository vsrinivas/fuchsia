// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

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

// Trying to load a nonexistant file should error.
TEST(ModuleSymbols, NonExistantFile) {
  ModuleSymbolsImpl module(
      TestSymbolModule::GetCheckedInTestFileName() + "_NONEXISTANT", "");
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

  ModuleSymbolsImpl module(
      TestSymbolModule::GetCheckedInTestFileName() + "_NONEXISTANT", "");
  Err err = module.Load();
  EXPECT_TRUE(err.has_error());
}

TEST(ModuleSymbols, Basic) {
  ModuleSymbolsImpl module(TestSymbolModule::GetCheckedInTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Make a symbol context with some load address to ensure that the addresses
  // round-trip properly.
  SymbolContext symbol_context(0x18000);

  // MyFunction() should have one implementation.
  std::vector<Location> addrs = module.ResolveInputLocation(
      symbol_context,
      InputLocation(Identifier(TestSymbolModule::kMyFunctionName)));
  ASSERT_EQ(1u, addrs.size());

  // On one occasion Clang generated a symbol file that listed many functions
  // in this file starting at offset 0. This obviously causes problems and
  // the test fails below with bafflingly incorrect line numbers. The problem
  // went away after forcing recompilation of that file. It might be an
  // intermittent Clang bug or some random corruption. If this assert hits,
  // check the function start addresses in the DWARF dump, there should be
  // no functions starting at offset 0 in the file.
  ASSERT_NE(0u, addrs[0].address());

  // That address should resolve back to the function name (don't know the
  // exact file path the compiler generated so just check the name).
  auto locations = module.ResolveInputLocation(
      symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_TRUE(locations[0].is_symbolized());
  EXPECT_TRUE(
      StringEndsWith(locations[0].file_line().file(), "/zxdb_symbol_test.cc"));
  EXPECT_EQ(TestSymbolModule::kMyFunctionLine, locations[0].file_line().line());
}

TEST(ModuleSymbols, LineDetailsForAddress) {
  ModuleSymbolsImpl module(TestSymbolModule::GetCheckedInTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Make a symbol context with some load address to ensure that the addresses
  // round-trip properly.
  SymbolContext symbol_context(0x18000);

  // Get the canonical file name to test.
  auto file_matches = module.FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  // Get address of line 28 which is a normal line with code on both sides.
  int const kLineToQuery = 28;
  ResolveOptions options;
  options.symbolize = false;
  std::vector<Location> addrs;
  addrs = module.ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, kLineToQuery)),
      options);
  ASSERT_LE(1u, addrs.size());
  auto locations = module.ResolveInputLocation(
      symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(kLineToQuery, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());

  // Lookup the line info. Normally we expect one line table entry for this but
  // don't want to assume that since the compiler could emit multiple entries
  // for it.
  LineDetails line_details =
      module.LineDetailsForAddress(symbol_context, addrs[0].address());
  EXPECT_EQ(file_name, line_details.file_line().file());
  EXPECT_EQ(kLineToQuery, line_details.file_line().line());
  ASSERT_FALSE(line_details.entries().empty());
  uint64_t begin_range = line_details.entries().front().range.begin();
  uint64_t end_range = line_details.entries().back().range.end();
  EXPECT_LT(begin_range, end_range);

  // The address before the beginning of the range should be the previous line.
  LineDetails prev_details =
      module.LineDetailsForAddress(symbol_context, begin_range - 1);
  EXPECT_EQ(kLineToQuery - 1, prev_details.file_line().line());
  EXPECT_EQ(file_name, prev_details.file_line().file());
  ASSERT_FALSE(prev_details.entries().empty());
  EXPECT_EQ(begin_range, prev_details.entries().back().range.end());

  // The end of the range (which is non-inclusive) should be the next line.
  LineDetails next_details =
      module.LineDetailsForAddress(symbol_context, end_range);
  EXPECT_EQ(kLineToQuery + 1, next_details.file_line().line());
  EXPECT_EQ(file_name, next_details.file_line().file());
  ASSERT_FALSE(next_details.entries().empty());
  EXPECT_EQ(end_range, next_details.entries().front().range.begin());
}

TEST(ModuleSymbols, ResolveLineInputLocation) {
  ModuleSymbolsImpl module(TestSymbolModule::GetCheckedInTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Make a symbol context with some load address to ensure that the addresses
  // round-trip properly.
  SymbolContext symbol_context(0x18000);

  // Get the canonical file name to test.
  auto file_matches = module.FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  // Basic one, look for line 27 which is a normal statement.
  ResolveOptions options;
  options.symbolize = false;
  std::vector<Location> addrs;
  addrs = module.ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, 27)), options);
  ASSERT_LE(1u, addrs.size());
  auto locations = module.ResolveInputLocation(
      symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(27, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());

  // Line 26 is a comment line, looking it up should get the following line.
  addrs = module.ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, 26)), options);
  ASSERT_LE(1u, addrs.size());
  locations = module.ResolveInputLocation(symbol_context,
                                          InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(27, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());

  // Line 15 is the beginning of the templatized function. There should be
  // two matches since its instantiated twice.
  addrs = module.ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, 15)), options);
  ASSERT_EQ(2u, addrs.size());
  locations = module.ResolveInputLocation(symbol_context,
                                          InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(15, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());
  locations = module.ResolveInputLocation(symbol_context,
                                          InputLocation(addrs[1].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(15, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());

  // Line 17 is only present in one of the two template instantiations.
  // We should only find it once (see note below about case #2).
  addrs = module.ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, 17)), options);
  ASSERT_TRUE(addrs.size() == 1u || addrs.size() == 2u);
  locations = module.ResolveInputLocation(symbol_context,
                                          InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(17, locations[0].file_line().line());
  if (addrs.size() == 2u) {
    // MSVC in debug mode will emit the full code in both instantiations of the
    // template which is valid. To be more robust this test allows that form
    // even though Clang doesn't do this. The important thing is that looking
    // up line 17 never gives us line 19 (which is the other template
    // instantiation).
    locations = module.ResolveInputLocation(symbol_context,
                                            InputLocation(addrs[1].address()));
    EXPECT_EQ(17, locations[0].file_line().line());
  }
}

TEST(ModuleSymbols, ResolveGlobalVariable) {
  ModuleSymbolsImpl module(TestSymbolModule::GetCheckedInTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  ResolveOptions options;
  options.symbolize = true;
  std::vector<Location> addrs;

  // Look up "kGlobal" which should be a variable of type "int" at some
  // nonzero location.
  addrs = module.ResolveInputLocation(
      symbol_context,
      InputLocation(TestSymbolModule::SplitName(TestSymbolModule::kGlobalName)),
      options);
  ASSERT_LE(1u, addrs.size());
  EXPECT_TRUE(addrs[0].symbol());
  const Variable* var = addrs[0].symbol().Get()->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(TestSymbolModule::kGlobalName, var->GetFullName());
  const Type* var_type = var->type().Get()->AsType();
  ASSERT_TRUE(var_type);
  EXPECT_EQ("int", var_type->GetFullName());

  // This number may change if we recompile the symbol test. That's OK, just
  // make sure it agrees with the relative address from symbol dump.
  EXPECT_EQ(0x2000u, addrs[0].address());

  // Look up the class static.
  addrs = module.ResolveInputLocation(symbol_context,
                                      InputLocation(TestSymbolModule::SplitName(
                                          TestSymbolModule::kClassStaticName)),
                                      options);
  ASSERT_LE(1u, addrs.size());
  EXPECT_TRUE(addrs[0].symbol());
  var = addrs[0].symbol().Get()->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(TestSymbolModule::kClassStaticName, var->GetFullName());
  var_type = var->type().Get()->AsType();
  ASSERT_TRUE(var_type);
  EXPECT_EQ("int", var_type->GetFullName());

  // This number may change if we recompile the symbol test. That's OK, just
  // make sure it agrees with the relative address from symbol dump.
  EXPECT_EQ(0x2004u, addrs[0].address());
}

TEST(ModuleSymbols, ResolvePLTEntry) {
  ModuleSymbolsImpl module(TestSymbolModule::GetCheckedInTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  auto addrs = module.ResolveInputLocation(
      symbol_context,
      InputLocation(
          Identifier(std::string(TestSymbolModule::kPltFunctionName) + "@plt")),
      ResolveOptions());

  ASSERT_EQ(1u, addrs.size());
  EXPECT_TRUE(addrs[0].is_valid());
  EXPECT_FALSE(addrs[0].is_symbolized());
  EXPECT_EQ(TestSymbolModule::kPltFunctionOffset, addrs[0].address());
}

}  // namespace zxdb
