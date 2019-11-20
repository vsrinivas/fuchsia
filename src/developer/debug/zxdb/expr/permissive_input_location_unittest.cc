// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/permissive_input_location.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"

namespace zxdb {

// The complexities of the FindName backend are all tested in its own unit tests. Here we're just
// concerned that the integration between FindName and the symbol system is working.
TEST(PermissiveInputLocation, ExpandAndResolve) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();

  auto& index_root = module_symbols->index().root();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  FindNameContext context(&setup.process(), symbol_context);

  // Index these functions:
  // - ::Foo()
  // - ::std::Foo()

  // ::Foo().
  const char kFooName[] = "Foo";
  auto foo = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  foo->set_assigned_name(kFooName);
  TestIndexedSymbol foo_indexed(module_symbols, &index_root, kFooName, foo);

  Location foo_location(ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x1234,
                        FileLine("file.cc", 34), 3, symbol_context, foo);
  module_symbols->AddSymbolLocations("::Foo", {foo_location});

  // "std" namespace.
  const char kStdName[] = "std";
  auto std_ns_symbol = fxl::MakeRefCounted<Namespace>(kStdName);
  auto std_ns = index_root.AddChild(IndexNode::Kind::kNamespace, kStdName);

  // ::std::Foo().
  auto std_foo = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  SymbolTestParentSetter std_foo_parent(std_foo, std_ns_symbol);
  std_foo->set_assigned_name(kFooName);
  TestIndexedSymbol std_foo_indexed(module_symbols, std_ns, kFooName, std_foo);

  Location std_foo_location(ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x5678,
                            FileLine("other.cc", 12), 4, symbol_context, std_foo);
  module_symbols->AddSymbolLocations("std::Foo", {std_foo_location});

  // Test ExpandPermissiveInputLocationNames().
  InputLocation foo_input(Identifier(IdentifierComponent("Foo")));
  auto expanded = ExpandPermissiveInputLocationNames(context, {foo_input});
  ASSERT_EQ(2u, expanded.size());
  EXPECT_EQ("::Foo", expanded[0].name.GetFullName());
  EXPECT_EQ("::std::Foo", expanded[1].name.GetFullName());

  // Additionally gives an address-based input.
  InputLocation addr_input(ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x9999);
  Location addr_location(addr_input.address, FileLine("addr.cc", 99), 1, symbol_context);
  module_symbols->AddSymbolLocations(addr_input.address, {addr_location});

  // Test ResolvePermissiveInputLocations().
  auto resolved = ResolvePermissiveInputLocations(&setup.process(), ResolveOptions(), context,
                                                  {addr_input, foo_input});
  EXPECT_EQ(3u, resolved.size());
  EXPECT_TRUE(addr_location.EqualsIgnoringSymbol(resolved[0]))
      << "got " << resolved[0].GetDebugString() << " expected " << addr_location.GetDebugString();
  EXPECT_TRUE(foo_location.EqualsIgnoringSymbol(resolved[1]))
      << "got " << resolved[1].GetDebugString() << " expected " << foo_location.GetDebugString();
  EXPECT_TRUE(std_foo_location.EqualsIgnoringSymbol(resolved[2]))
      << "got " << resolved[2].GetDebugString() << " expected "
      << std_foo_location.GetDebugString();
}

}  // namespace zxdb
