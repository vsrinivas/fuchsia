// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_location.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

TEST(FormatLocation, FormatLocation_Function) {
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  FormatLocationOptions no_addrs_no_params;
  no_addrs_no_params.always_show_addresses = false;
  no_addrs_no_params.show_params = false;
  no_addrs_no_params.show_file_line = true;
  EXPECT_EQ("<invalid address>", FormatLocation(Location(), no_addrs_no_params).AsString());

  // Address-only location.
  EXPECT_EQ(
      "0x12345",
      FormatLocation(Location(Location::State::kAddress, 0x12345), no_addrs_no_params).AsString());

  // Function-only location.
  fxl::RefPtr<Function> function(fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
  function->set_assigned_name("Func");
  function->set_code_ranges(AddressRanges(AddressRange(0x1200, 0x1300)));
  EXPECT_EQ(
      "Func() + 0x34 (no line info)",
      FormatLocation(Location(0x1234, FileLine(), 0, symbol_context, function), no_addrs_no_params)
          .AsString());

  // Same as above but location is before the function address (probably something is corrupt). It
  // should omit the offset.
  EXPECT_EQ("Func()", FormatLocation(Location(0x1100, FileLine(), 0, symbol_context, function),
                                     no_addrs_no_params)
                          .AsString());

  // File/line present but not shown.
  FormatLocationOptions no_file_line;
  no_file_line.always_show_addresses = false;
  no_file_line.show_params = false;
  no_file_line.show_file_line = false;
  Location loc(0x1234, FileLine("/path/foo.cc", 21), 0, symbol_context, function);
  EXPECT_EQ("Func() + 0x34", FormatLocation(loc, no_file_line).AsString());

  // File/line not shown and address exactly matches the symbol (the offset should be omitted).
  Location func_loc(0x1234, FileLine("/path/foo.cc", 21), 0, symbol_context, function);
  EXPECT_EQ("Func() + 0x34", FormatLocation(func_loc, no_file_line).AsString());

  // File/line-only location.
  EXPECT_EQ("0x1234 • /path/foo.cc:21",
            FormatLocation(Location(0x1234, FileLine("/path/foo.cc", 21), 0, symbol_context),
                           no_addrs_no_params)
                .AsString());

  // Full location.
  FormatLocationOptions always_addr;
  always_addr.always_show_addresses = true;
  always_addr.show_params = false;
  always_addr.show_file_line = true;
  EXPECT_EQ("0x1234, Func() • /path/foo.cc:21", FormatLocation(loc, always_addr).AsString());
  EXPECT_EQ("Func() • /path/foo.cc:21", FormatLocation(loc, no_addrs_no_params).AsString());

  // Test with file shortening. This target has no files so everything is unique and will be
  // shortened.
  ProcessSymbolsTestSetup setup;
  FormatLocationOptions with_target;
  with_target.show_file_line = true;
  with_target.target_symbols = &setup.target();
  EXPECT_EQ("Func() • foo.cc:21", FormatLocation(loc, with_target).AsString());

  // Use the same parameters but force showing the whole path.
  with_target.show_file_path = true;
  EXPECT_EQ("Func() • /path/foo.cc:21", FormatLocation(loc, with_target).AsString());
}

TEST(FormatLocation, FormatLocation_ELF) {
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  FormatLocationOptions options;
  options.always_show_addresses = false;
  options.show_params = false;

  // Address exactly at the beginning of the ELF symbol.
  constexpr uint64_t kFunctionAddress = 0x1000;
  fxl::RefPtr<ElfSymbol> elf_symbol(fxl::MakeRefCounted<ElfSymbol>(
      fxl::WeakPtr<ModuleSymbols>(),
      ElfSymbolRecord(ElfSymbolType::kPlt, kFunctionAddress, 0, "memset")));
  EXPECT_EQ(
      "memset",
      FormatLocation(Location(kFunctionAddress, FileLine(), 0, symbol_context, elf_symbol), options)
          .AsString());

  // Address with an offset from the beginning.
  EXPECT_EQ("memset + 0x6",
            FormatLocation(
                Location(kFunctionAddress + 6, FileLine(), 0, symbol_context, elf_symbol), options)
                .AsString());
}

TEST(FormatLocation, DescribeFileLine) {
  FileLine fl("/path/to/foo.cc", 21);
  EXPECT_EQ("/path/to/foo.cc:21", DescribeFileLine(nullptr, fl));

  // Missing line number.
  EXPECT_EQ("/path/foo.cc:?", DescribeFileLine(nullptr, FileLine("/path/foo.cc", 0)));

  // Missing both.
  EXPECT_EQ("?:?", DescribeFileLine(nullptr, FileLine()));

  // Pass an TargetSymbols to trigger path shortening. Since the TargetSymbols has no files to
  // match, the name will be unique and we'll get just the name part.
  ProcessSymbolsTestSetup setup;
  EXPECT_EQ("foo.cc:21", DescribeFileLine(&setup.target(), fl));
}

}  // namespace zxdb
