// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/input_location_parser.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

TEST(InputLocationParser, Parse) {
  InputLocation location;

  SymbolContext relative_context = SymbolContext::ForRelativeAddresses();

  // Valid symbol (including colons).
  Err err = ParseInputLocation(nullptr, "Foo::Bar", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kSymbol, location.type);
  EXPECT_EQ(R"("Foo"; ::"Bar")", location.symbol.GetDebugName());

  // Valid file/line.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "foo/bar.cc:123", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kLine, location.type);
  EXPECT_EQ("foo/bar.cc", location.line.file());
  EXPECT_EQ(123, location.line.line());

  // Invalid file/line.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "foo/bar.cc:123x", &location);
  EXPECT_TRUE(err.has_error());

  // Valid hex address with *.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "*0x12345f", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kAddress, location.type);
  EXPECT_EQ(0x12345fu, location.address);

  // Valid hex address without a *.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "0x12345f", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kAddress, location.type);
  EXPECT_EQ(0x12345fu, location.address);

  // Decimal number with "*" override should be an address.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "*21", &location);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(InputLocation::Type::kAddress, location.type);
  EXPECT_EQ(21u, location.address);

  // Invalid address.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "*2134x", &location);
  EXPECT_TRUE(err.has_error());

  // Line number with no Frame for context.
  location = InputLocation();
  err = ParseInputLocation(nullptr, "21", &location);
  EXPECT_TRUE(err.has_error());

  // Implicit file name and valid frame but the location has no file name.
  debug_ipc::StackFrame stack_frame(0x1234, 0x12345678, 0x12345678);
  MockFrame frame_no_file(
      nullptr, nullptr, stack_frame,
      Location(0x1234, FileLine(), 0, relative_context, LazySymbol()));
  location = InputLocation();
  err = ParseInputLocation(&frame_no_file, "21", &location);
  EXPECT_TRUE(err.has_error());

  // Valid implicit file name.
  std::string file = "foo.cc";
  MockFrame frame_valid(
      nullptr, nullptr, stack_frame,
      Location(0x1234, FileLine(file, 12), 0, relative_context, LazySymbol()));
  location = InputLocation();
  err = ParseInputLocation(&frame_valid, "21", &location);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(file, location.line.file());
  EXPECT_EQ(21, location.line.line());
}

TEST(InputLocation, ResolveInputLocation) {
  ProcessSymbolsTestSetup symbols;
  auto owning_mod_sym = std::make_unique<MockModuleSymbols>("mod.so");
  MockModuleSymbols* module_symbols = owning_mod_sym.get();

  constexpr uint64_t kModuleLoadAddress = 0x10000;
  SymbolContext symbol_context(kModuleLoadAddress);
  symbols.InjectModule("mid.so", "1234", kModuleLoadAddress,
                       std::move(owning_mod_sym));

  // Resolve to nothing.
  Location output;
  Err err = ResolveUniqueInputLocation(&symbols.process(), nullptr, "Foo",
                                       false, &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Nothing matching this symbol was found.", err.msg());

  Location expected(0x12345678, FileLine("file.cc", 12), 0, symbol_context);

  // Resolve to one location (success) case.
  module_symbols->AddSymbolLocations("Foo", {expected});
  err = ResolveUniqueInputLocation(&symbols.process(), nullptr, "Foo", false,
                                   &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(expected.address(), output.address());

  // Resolve to lots of locations, it should give suggestions. Even though we
  // didn't request symbols, the result should be symbolized.
  std::vector<Location> expected_locations;
  for (int i = 0; i < 15; i++) {
    // The address and line numbers count up for each match.
    expected_locations.emplace_back(
        0x12345000 + i, FileLine("file.cc", 100 + i), 0, symbol_context);
  }
  module_symbols->AddSymbolLocations("Foo", expected_locations);

  // Resolve to all of them.
  std::vector<Location> output_locations;
  err = ResolveInputLocations(&symbols.process(), nullptr, "Foo", false,
                              &output_locations);
  EXPECT_FALSE(err.has_error());

  // The result should be the same as the input but not symbolized (we
  // requested no symbolization).
  ASSERT_EQ(expected_locations.size(), output_locations.size());
  for (size_t i = 0; i < expected_locations.size(); i++) {
    EXPECT_EQ(expected_locations[i].address(), output_locations[i].address());
    EXPECT_FALSE(output_locations[i].has_symbols());
  }

  // Try to resolve one of them. Since there are many this will fail. We
  // requested no symbolization but the error message should still be
  // symbolized.
  err = ResolveUniqueInputLocation(&symbols.process(), nullptr, "Foo", false,
                                   &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(R"(This resolves to more than one location. Could be:
 • file.cc:100 = 0x12345000
 • file.cc:101 = 0x12345001
 • file.cc:102 = 0x12345002
 • file.cc:103 = 0x12345003
 • file.cc:104 = 0x12345004
 • file.cc:105 = 0x12345005
 • file.cc:106 = 0x12345006
 • file.cc:107 = 0x12345007
 • file.cc:108 = 0x12345008
 • file.cc:109 = 0x12345009
...5 more omitted...
)",
            err.msg());
}

}  // namespace zxdb
