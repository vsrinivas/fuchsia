// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/dwarf_binary_impl.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
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

// Trying to load a nonexistent file should error.
TEST(ModuleSymbols, NonExistantFile) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName() + "_NONEXISTANT", "");
  // Should fail to load.
  EXPECT_FALSE(setup.Init().ok());
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

  // The load should fail.
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName() + "_NONEXISTANT", "");
  ASSERT_FALSE(setup.Init("", false).ok());
}

TEST(ModuleSymbols, GetMappedLength) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(), "");
  ASSERT_TRUE(setup.Init("/build_dir").ok());

  // The checked-in test .so's last PROGBITS segment record is:
  //
  //   [15] .got.plt          PROGBITS         0000000000002000  00002000
  //       0000000000000030  0000000000000000  WA       0     0     8
  //
  // So 0x3008 offset + 0x30 = 0x2030 ending offset. This will likely change if the checked-in
  // test .so is updated. Just verify the results with "readelf -S"
  EXPECT_EQ(0x2030u, setup.symbols()->GetMappedLength());
}

TEST(ModuleSymbols, Basic) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(), "");
  ASSERT_TRUE(setup.Init("/build_dir").ok());

  // Make a symbol context with some load address to ensure that the addresses round-trip properly.
  SymbolContext symbol_context(0x18000);

  // MyFunction() should have one implementation.
  std::vector<Location> addrs = setup.symbols()->ResolveInputLocation(
      symbol_context,
      InputLocation(Identifier(IdentifierComponent(TestSymbolModule::kMyFunctionName))));
  ASSERT_EQ(1u, addrs.size());

  // On one occasion Clang generated a symbol file that listed many functions in this file starting
  // at offset 0. This obviously causes problems and the test fails below with bafflingly incorrect
  // line numbers. The problem went away after forcing recompilation of that file. It might be an
  // intermittent Clang bug or some random corruption. If this assert hits, check the function start
  // addresses in the DWARF dump, there should be no functions starting at offset 0 in the file.
  ASSERT_NE(0u, addrs[0].address());

  // That address should resolve back to the function name (don't know the exact file path the
  // compiler generated so just check the name).
  auto locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_TRUE(locations[0].is_symbolized());
  EXPECT_TRUE(StringEndsWith(locations[0].file_line().file(), "/zxdb_symbol_test.cc"));
  EXPECT_EQ(109, locations[0].file_line().line());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());

  // The function symbol should have a compilation unit with a C-style language defined and the name
  // should contain the file.
  ASSERT_TRUE(locations[0].symbol());
  fxl::RefPtr<CompileUnit> unit = locations[0].symbol().Get()->GetCompileUnit();
  ASSERT_TRUE(unit);
  EXPECT_NE(std::string::npos, unit->name().find("zxdb_symbol_test.cc"));
  EXPECT_TRUE(DwarfLangIsCFamily(unit->language()));
}

// Tests that querying an address far from the last address in the module won't return anything.
TEST(ModuleSymbols, OffEnd) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(), "");
  ASSERT_TRUE(setup.Init("/build_dir").ok());

  constexpr uint64_t kLoadAddress = 0x100000;
  SymbolContext symbol_context(kLoadAddress);

  // Check an address far past the end of the test module.
  std::vector<Location> addrs = setup.symbols()->ResolveInputLocation(
      symbol_context, InputLocation(kLoadAddress + 0x1000000000));
  ASSERT_EQ(1u, addrs.size());

  // It should not have a matching symbol (the last ELF symbol in the module shouldn't match it
  // just because it's the previous thing).
  EXPECT_FALSE(addrs[0].has_symbols());
}

TEST(ModuleSymbols, LineDetailsForAddress) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(), "");
  ASSERT_TRUE(setup.Init("/build_dir").ok());

  // Make a symbol context with some load address to ensure that the addresses round-trip properly.
  SymbolContext symbol_context(0x18000);

  // Get the canonical file name to test.
  auto file_matches = setup.symbols()->FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  // The file name should be canonical and lack any redundant "." or "..".
  EXPECT_EQ("../../src/developer/debug/zxdb/symbols/test_data/line_lookup_symbol_test.cc",
            file_name);

  // Get address of line 28 which is a normal line with code on both sides.
  int const kLineToQuery = 28;
  ResolveOptions options;
  options.symbolize = false;
  std::vector<Location> addrs;
  addrs = setup.symbols()->ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, kLineToQuery)), options);
  ASSERT_LE(1u, addrs.size());
  auto locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(kLineToQuery, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());

  // Lookup the line info. Normally we expect one line table entry for this but don't want to assume
  // that since the compiler could emit multiple entries for it.
  LineDetails line_details =
      setup.symbols()->LineDetailsForAddress(symbol_context, addrs[0].address(), false);
  EXPECT_EQ(file_name, line_details.file_line().file());
  EXPECT_EQ(kLineToQuery, line_details.file_line().line());
  EXPECT_EQ("/build_dir", line_details.file_line().comp_dir());
  ASSERT_FALSE(line_details.entries().empty());
  uint64_t begin_range = line_details.entries().front().range.begin();
  uint64_t end_range = line_details.entries().back().range.end();
  EXPECT_LT(begin_range, end_range);

  // The address before the beginning of the range should be the previous line.
  LineDetails prev_details =
      setup.symbols()->LineDetailsForAddress(symbol_context, begin_range - 1, false);
  EXPECT_EQ(kLineToQuery - 1, prev_details.file_line().line());
  EXPECT_EQ(file_name, prev_details.file_line().file());
  EXPECT_EQ("/build_dir", prev_details.file_line().comp_dir());
  ASSERT_FALSE(prev_details.entries().empty());
  EXPECT_EQ(begin_range, prev_details.entries().back().range.end());

  // The end of the range (which is non-inclusive) should be the next line.
  LineDetails next_details =
      setup.symbols()->LineDetailsForAddress(symbol_context, end_range, false);
  EXPECT_EQ(kLineToQuery + 1, next_details.file_line().line());
  EXPECT_EQ(file_name, next_details.file_line().file());
  EXPECT_EQ("/build_dir", next_details.file_line().comp_dir());
  ASSERT_FALSE(next_details.entries().empty());
  EXPECT_EQ(end_range, next_details.entries().front().range.begin());
}

TEST(ModuleSymbols, ResolveLineInputLocation) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(), "");
  ASSERT_TRUE(setup.Init("/build_dir").ok());

  // Make a symbol context with some load address to ensure that the addresses round-trip properly.
  SymbolContext symbol_context(0x18000);

  // Get the canonical file name to test.
  auto file_matches = setup.symbols()->FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  // Basic one, look for line 27 which is a normal statement.
  ResolveOptions options;
  options.symbolize = false;
  std::vector<Location> addrs;
  addrs = setup.symbols()->ResolveInputLocation(symbol_context,
                                                InputLocation(FileLine(file_name, 27)), options);
  ASSERT_LE(1u, addrs.size());
  auto locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(27, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());

  // Absolute path should be supported.
  locations = setup.symbols()->ResolveInputLocation(
      symbol_context, InputLocation(FileLine("/build_dir/" + file_name, 27)), options);
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(addrs[0].address(), locations[0].address());

  // Line 26 is a comment line, looking it up should get the following line.
  addrs = setup.symbols()->ResolveInputLocation(symbol_context,
                                                InputLocation(FileLine(file_name, 26)), options);
  ASSERT_LE(1u, addrs.size());
  locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(27, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());

  // Line 15 is the beginning of the templatized function. There should be two matches since its
  // instantiated twice.
  addrs = setup.symbols()->ResolveInputLocation(symbol_context,
                                                InputLocation(FileLine(file_name, 15)), options);
  ASSERT_EQ(2u, addrs.size());
  locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(15, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());
  locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[1].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(15, locations[0].file_line().line());
  EXPECT_EQ(file_name, locations[0].file_line().file());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());

  // Line 17 is only present in one of the two template instantiations.  We should only find it once
  // (see note below about case #2).
  addrs = setup.symbols()->ResolveInputLocation(symbol_context,
                                                InputLocation(FileLine(file_name, 17)), options);
  ASSERT_TRUE(addrs.size() == 1u || addrs.size() == 2u);
  locations =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[0].address()));
  ASSERT_EQ(1u, locations.size());
  EXPECT_EQ(17, locations[0].file_line().line());
  EXPECT_EQ("/build_dir", locations[0].file_line().comp_dir());
  if (addrs.size() == 2u) {
    // MSVC in debug mode will emit the full code in both instantiations of the template which is
    // valid. To be more robust this test allows that form even though Clang doesn't do this. The
    // important thing is that looking up line 17 never gives us line 19 (which is the other
    // template instantiation).
    locations =
        setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(addrs[1].address()));
    EXPECT_EQ(17, locations[0].file_line().line());
  }
}

// Tests that we can match inline function call lines. As part of
// https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=112572, Clang doesn't always emit line
// information for the call line of an inlined function.
//
// This uses the current (rather than checked-in) symbol file to verify that we can match the
// inlined call location in whatever compiler config is running right now. Generally this will
// happen more in optimized builds.
TEST(ModuleSymbols, ResolveLineInputLocation_Inlines) {
  TestSymbolModule setup(TestSymbolModule::GetTestFileName(), "");
  ASSERT_TRUE(setup.Init("/build_dir").ok());

  // Make a symbol context with some load address to ensure that the addresses round-trip properly.
  SymbolContext symbol_context(0x18000);

  // Get the canonical file name to test.
  auto file_matches = setup.symbols()->FindFileMatches("line_lookup_symbol_test.cc");
  ASSERT_EQ(1u, file_matches.size());
  const std::string file_name = file_matches[0];

  constexpr int kInlineFnFirstLine = 34;   // Declaration line of the InlineCall().
  constexpr int kInlineFnSecondLine = 35;  // Body of the inline function.
  constexpr int kInlineCallLine = 42;      // Where the inlined function is called from.

  ResolveOptions options;
  options.symbolize = true;

  // This line calls an inline function.
  std::vector<Location> addrs = setup.symbols()->ResolveInputLocation(
      symbol_context, InputLocation(FileLine(file_name, kInlineCallLine)), options);
  ASSERT_EQ(1u, addrs.size());

  // Here we tolerate the match as matching the call line (what we put in) OR the actual inlined
  // function (accepting either the declaration or body line of it). If the function is actually
  // inlined, the round-trip query of that address could legitimately match the first instruction of
  // the inlined function. We want to be flexible about how the compiler represents this.
  int matched_line = addrs[0].file_line().line();
  EXPECT_TRUE(matched_line == kInlineFnFirstLine || matched_line == kInlineFnSecondLine ||
              matched_line == kInlineCallLine)
      << "Expecting the matched line of " << matched_line << " to be one of {" << kInlineFnFirstLine
      << ", " << kInlineFnSecondLine << ", " << kInlineCallLine << "}";
}

TEST(ModuleSymbols, ResolveGlobalVariable) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(), "");
  ASSERT_TRUE(setup.Init().ok());

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  ResolveOptions options;
  options.symbolize = true;
  std::vector<Location> addrs;

  // Look up "kGlobal" which should be a variable of type "int" at some nonzero location.
  Identifier global_name = TestSymbolModule::SplitName(TestSymbolModule::kGlobalName);
  addrs =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(global_name), options);
  ASSERT_LE(1u, addrs.size());
  EXPECT_TRUE(addrs[0].symbol());
  const Variable* var = addrs[0].symbol().Get()->As<Variable>();
  ASSERT_TRUE(var);
  EXPECT_EQ(TestSymbolModule::kGlobalName, var->GetFullName());
  const Type* var_type = var->type().Get()->As<Type>();
  ASSERT_TRUE(var_type);
  EXPECT_EQ("int", var_type->GetFullName());

  // This variable doesn't have an address because in the current checked-in file, it's expressed as
  // a DWARF expression that the SymbolModule doesn't evaluate. In a normal expression, the
  // symbol match will trigger the necessary logic in the expression evaluation system. This
  // expression might change and that's OK.
  EXPECT_EQ(0u, addrs[0].address());

  // Look up the class static.
  addrs = setup.symbols()->ResolveInputLocation(
      symbol_context,
      InputLocation(TestSymbolModule::SplitName(TestSymbolModule::kClassStaticName)), options);
  ASSERT_LE(1u, addrs.size());
  EXPECT_TRUE(addrs[0].symbol());
  var = addrs[0].symbol().Get()->As<Variable>();
  ASSERT_TRUE(var);
  EXPECT_EQ(TestSymbolModule::kClassStaticName, var->GetFullName());
  var_type = var->type().Get()->As<Type>();
  ASSERT_TRUE(var_type);
  EXPECT_EQ("int", var_type->GetFullName());

  // As above, there's no address for the current symbol file.
  EXPECT_EQ(0u, addrs[0].address());

  // Annotate the global variable as a register. This lookup should fail since registers can't be
  // looked up in the symbols (this tests that ModuleSymbolsImpl filters out bad special component
  // types).
  Identifier register_name;
  register_name.AppendComponent(
      IdentifierComponent(SpecialIdentifier::kRegister, TestSymbolModule::kGlobalName));
  addrs =
      setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(register_name), options);
  ASSERT_TRUE(addrs.empty());
}

TEST(ModuleSymbols, ResolvePLTEntry) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(),
                         TestSymbolModule::GetStrippedCheckedInTestFileName());
  ASSERT_TRUE(setup.Init().ok());

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  ResolveOptions options;
  options.symbolize = true;

  // Name->PLT symbol.
  auto result = setup.symbols()->ResolveInputLocation(
      symbol_context,
      InputLocation(Identifier(
          IdentifierComponent(SpecialIdentifier::kPlt, TestSymbolModule::kPltFunctionName))),
      options);

  ASSERT_EQ(1u, result.size());
  EXPECT_TRUE(result[0].is_valid());
  EXPECT_EQ(TestSymbolModule::kPltFunctionOffset, result[0].address());

  const ElfSymbol* elf_symbol = result[0].symbol().Get()->As<ElfSymbol>();
  ASSERT_TRUE(elf_symbol);
  EXPECT_EQ(ElfSymbolType::kPlt, elf_symbol->elf_type());
  EXPECT_EQ(TestSymbolModule::kPltFunctionName, elf_symbol->linkage_name());

  // Now look up the address and expect to get the symbol back.
  result = setup.symbols()->ResolveInputLocation(
      symbol_context, InputLocation(TestSymbolModule::kPltFunctionOffset), options);
  ASSERT_EQ(1u, result.size());

  elf_symbol = result[0].symbol().Get()->As<ElfSymbol>();
  ASSERT_TRUE(elf_symbol);
  EXPECT_EQ(ElfSymbolType::kPlt, elf_symbol->elf_type());
  EXPECT_EQ(TestSymbolModule::kPltFunctionName, elf_symbol->linkage_name());
}

TEST(ModuleSymbols, ResolveMainFunction) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(),
                         TestSymbolModule::GetStrippedCheckedInTestFileName());
  ASSERT_TRUE(setup.Init().ok());

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // The sample module is a shared library with no main function, so there should be nothing found.
  InputLocation input_loc((Identifier(IdentifierComponent(SpecialIdentifier::kMain))));
  ResolveOptions options;
  auto addrs = setup.symbols()->ResolveInputLocation(symbol_context, input_loc, options);
  EXPECT_TRUE(addrs.empty());

  // Inject a function named "main" (but not marked in the symbols as the official main function).
  // This is kind of a hack. The ModuleSymbolsImpl is using a real symbol file and need to be able
  // to generate the function symbol from the SymbolRef for this call to succeed. So we can't just
  // inject a fake SymbolRef. Instead, redirect "main" in the index to an existing function
  // ("MyFunction").
  auto my_function_matches = setup.symbols()->index_.FindExact(
      Identifier(IdentifierComponent(TestSymbolModule::kMyFunctionName)));
  ASSERT_EQ(1u, my_function_matches.size());
  auto main_node = setup.symbols()->index_.root().AddChild(IndexNode::Kind::kFunction, "main");
  main_node->AddDie(my_function_matches[0]);

  // Query for $main again. Since nothing is marked as the main function, the one named "main"
  // should be returned. Since we redirected the index above, this will actually be "MyFunction".
  addrs = setup.symbols()->ResolveInputLocation(symbol_context, input_loc, options);
  ASSERT_EQ(1u, addrs.size());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName, addrs[0].symbol().Get()->GetFullName());

  // Now mark a different function as the official main one (kNamespaceFunctionName).
  auto anon_function_matches = setup.symbols()->index_.FindExact(
      TestSymbolModule::SplitName(TestSymbolModule::kNamespaceFunctionName));
  ASSERT_EQ(1u, anon_function_matches.size());
  setup.symbols()->index_.main_functions().push_back(anon_function_matches[0]);

  // Query again. Now that a function is explicitly marked as the main one,
  // only it should be returned.
  addrs = setup.symbols()->ResolveInputLocation(symbol_context, input_loc, options);
  ASSERT_EQ(1u, addrs.size());
  EXPECT_EQ(TestSymbolModule::kNamespaceFunctionName, addrs[0].symbol().Get()->GetFullName());
}

TEST(ModuleSymbols, SkipPrologue) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(),
                         TestSymbolModule::GetStrippedCheckedInTestFileName());
  ASSERT_TRUE(setup.Init().ok());

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  InputLocation input_fn_loc((Identifier(IdentifierComponent(TestSymbolModule::kMyFunctionName))));

  // Query the function by name with no prologue skipping.
  ResolveOptions no_skip_options;
  no_skip_options.symbolize = true;
  no_skip_options.skip_function_prologue = false;
  auto no_skip_addrs =
      setup.symbols()->ResolveInputLocation(symbol_context, input_fn_loc, no_skip_options);
  ASSERT_EQ(1u, no_skip_addrs.size());
  EXPECT_EQ(TestSymbolModule::kMyFunctionAddress, no_skip_addrs[0].address());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName,
            no_skip_addrs[0].symbol().Get()->As<Function>()->GetFullName());

  // Now with prologue skipping.
  ResolveOptions skip_options;
  skip_options.symbolize = true;
  skip_options.skip_function_prologue = true;
  auto skip_addrs =
      setup.symbols()->ResolveInputLocation(symbol_context, input_fn_loc, skip_options);
  ASSERT_EQ(1u, skip_addrs.size());
  EXPECT_EQ(no_skip_addrs[0].address() + TestSymbolModule::kMyFunctionPrologueSize,
            skip_addrs[0].address());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName,
            skip_addrs[0].symbol().Get()->As<Function>()->GetFullName());

  // Query by line. No skipping.
  InputLocation input_line_loc(FileLine("zxdb_symbol_test.cc", TestSymbolModule::kMyFunctionLine));
  no_skip_addrs =
      setup.symbols()->ResolveInputLocation(symbol_context, input_line_loc, no_skip_options);
  ASSERT_EQ(1u, no_skip_addrs.size());
  EXPECT_EQ(TestSymbolModule::kMyFunctionAddress, no_skip_addrs[0].address());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName,
            no_skip_addrs[0].symbol().Get()->As<Function>()->GetFullName());

  // With skipping.
  skip_addrs = setup.symbols()->ResolveInputLocation(symbol_context, input_line_loc, skip_options);
  ASSERT_EQ(1u, skip_addrs.size());
  EXPECT_EQ(TestSymbolModule::kMyFunctionAddress + TestSymbolModule::kMyFunctionPrologueSize,
            skip_addrs[0].address());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName,
            skip_addrs[0].symbol().Get()->As<Function>()->GetFullName());

  // Query by address. No skipping.
  InputLocation input_addr_loc(TestSymbolModule::kMyFunctionAddress);
  no_skip_addrs =
      setup.symbols()->ResolveInputLocation(symbol_context, input_addr_loc, no_skip_options);
  ASSERT_EQ(1u, no_skip_addrs.size());
  EXPECT_EQ(TestSymbolModule::kMyFunctionAddress, no_skip_addrs[0].address());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName,
            no_skip_addrs[0].symbol().Get()->As<Function>()->GetFullName());

  // With skipping.
  skip_addrs = setup.symbols()->ResolveInputLocation(symbol_context, input_addr_loc, skip_options);
  ASSERT_EQ(1u, skip_addrs.size());
  EXPECT_EQ(TestSymbolModule::kMyFunctionAddress + TestSymbolModule::kMyFunctionPrologueSize,
            skip_addrs[0].address());
  EXPECT_EQ(TestSymbolModule::kMyFunctionName,
            skip_addrs[0].symbol().Get()->As<Function>()->GetFullName());
}

TEST(ModuleSymbols, ElfSymbols) {
  TestSymbolModule setup(TestSymbolModule::GetCheckedInTestFileName(),
                         TestSymbolModule::GetStrippedCheckedInTestFileName());
  ASSERT_TRUE(setup.Init().ok());

  // Give it a non-relative context to make sure that things are relative-ized going in and out.
  SymbolContext symbol_context(0x1000000);

  // Virtual tables have ELF symbols but not DWARF symbols, so to test that we read the ELF symbols
  // properly, look one up.
  const char kVirtualDerivedVtableName[] = "_ZTV14VirtualDerived";
  const char kVirtualDerivedVtableUnmangledName[] = "vtable for VirtualDerived";
  Identifier vtable_identifier(
      IdentifierComponent(SpecialIdentifier::kElf, kVirtualDerivedVtableName));
  std::vector<Location> result = setup.symbols()->ResolveInputLocation(
      symbol_context, InputLocation(vtable_identifier), ResolveOptions());
  ASSERT_EQ(1u, result.size());

  // It should have found the ELF symbol.
  ASSERT_TRUE(result[0].symbol());
  auto elf_symbol = result[0].symbol().Get()->As<ElfSymbol>();
  ASSERT_TRUE(elf_symbol);
  EXPECT_EQ(kVirtualDerivedVtableName, elf_symbol->linkage_name());
  EXPECT_EQ(kVirtualDerivedVtableUnmangledName, elf_symbol->GetFullName());

  // The returned address should match the symbol info except for relative/absolute.
  uint64_t absolute_addr = result[0].address();
  EXPECT_EQ(absolute_addr, symbol_context.RelativeToAbsolute(elf_symbol->relative_address()));

  // Looking up by that address should give back the name.
  result = setup.symbols()->ResolveInputLocation(symbol_context, InputLocation(absolute_addr),
                                                 ResolveOptions());
  ASSERT_EQ(1u, result.size());

  // Symbols, names, and addresses should match as above.
  ASSERT_TRUE(result[0].symbol());
  elf_symbol = result[0].symbol().Get()->As<ElfSymbol>();
  ASSERT_TRUE(elf_symbol);
  EXPECT_EQ(kVirtualDerivedVtableName, elf_symbol->linkage_name());
  EXPECT_EQ(kVirtualDerivedVtableUnmangledName, elf_symbol->GetFullName());
  EXPECT_EQ(result[0].address(), symbol_context.RelativeToAbsolute(elf_symbol->relative_address()));
}

// Loads the stripped binary and ensures that exported symbols can still be found (these symbols
// will be present only in the ELF ".dynsym" table).
TEST(ModuleSymbols, StrippedElfSymbols) {
  // Supply only the stripped binary name.
  TestSymbolModule setup(TestSymbolModule::GetStrippedCheckedInTestFileName(),
                         TestSymbolModule::GetStrippedCheckedInTestFileName());
  ASSERT_TRUE(setup.Init().ok());

  // Give it a non-relative context to make sure that things are relative-ized going in and out.
  SymbolContext symbol_context(0x1000000);

  const char kSymName[] = "_Z9GetIntPtrv";
  std::vector<Location> result = setup.symbols()->ResolveInputLocation(
      symbol_context,
      InputLocation(Identifier(IdentifierComponent(SpecialIdentifier::kElf, kSymName))),
      ResolveOptions());
  ASSERT_EQ(1u, result.size());

  // It should have found the ELF symbol.
  ASSERT_TRUE(result[0].symbol());
  auto elf_symbol = result[0].symbol().Get()->As<ElfSymbol>();
  ASSERT_TRUE(elf_symbol);
  EXPECT_EQ(kSymName, elf_symbol->linkage_name());
  EXPECT_EQ("GetIntPtr()", elf_symbol->GetFullName());
}

}  // namespace zxdb
