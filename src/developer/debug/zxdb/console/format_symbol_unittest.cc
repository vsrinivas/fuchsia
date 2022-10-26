// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_symbol.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/call_site.h"
#include "src/developer/debug/zxdb/symbols/call_site_parameter.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_factory.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

TEST(FormatSymbol, Variable) {
  auto int32_type = MakeInt32Type();

  std::vector<VariableLocation::Entry> loc_entries;
  loc_entries.resize(2);
  loc_entries[0].range = AddressRange(0x1000, 0x2000);
  loc_entries[0].expression = DwarfExpr({0x30,  // DW_OP_lit0
                                         0x71,  // DW_OP_breg1
                                         1});   // 1 (param for breg).

  loc_entries[1].range = AddressRange(0x3000, 0x4000);
  loc_entries[1].expression = DwarfExpr({0x31});  // DW_OP_lit1

  auto var = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable, "my_var", int32_type,
                                           VariableLocation(loc_entries));

  // Provide a DIE offset of this symbol so we can test its output. The MockSymbolFactory will
  // set this on the symbol in SetMockSymbol to the requested offset.
  MockSymbolFactory symbol_factory;
  symbol_factory.SetMockSymbol(0x12345, var);

  FormatSymbolOptions opts;
  opts.arch = debug::Arch::kX64;

  // Format expressions as bytes.
  opts.dwarf_expr = FormatSymbolOptions::DwarfExpr::kBytes;
  OutputBuffer out = FormatSymbol(nullptr, var.get(), opts);
  const char kExpectedBytes[] =
      "Variable: my_var\n"
      "  Type: int32_t\n"
      "  DWARF tag: DW_TAG_variable (0x34) @ offset 0x12345\n"
      "  DWARF location (address range + DWARF expression):\n"
      "    [0x1000, 0x2000): 0x30 0x71 0x01\n"
      "    [0x3000, 0x4000): 0x31\n";
  EXPECT_EQ(kExpectedBytes, out.AsString());

  // Format expressions as DWARF operations.
  opts.dwarf_expr = FormatSymbolOptions::DwarfExpr::kOps;
  out = FormatSymbol(nullptr, var.get(), opts);
  const char kExpectedOps[] =
      "Variable: my_var\n"
      "  Type: int32_t\n"
      "  DWARF tag: DW_TAG_variable (0x34) @ offset 0x12345\n"
      "  DWARF location (address range + DWARF expression):\n"
      "    [0x1000, 0x2000): DW_OP_lit0, DW_OP_breg1(1)\n"
      "    [0x3000, 0x4000): DW_OP_lit1\n";
  EXPECT_EQ(kExpectedOps, out.AsString());

  // Pretty formatting of expressions.
  opts.dwarf_expr = FormatSymbolOptions::DwarfExpr::kPretty;
  out = FormatSymbol(nullptr, var.get(), opts);
  const char kExpectedPretty[] =
      "Variable: my_var\n"
      "  Type: int32_t\n"
      "  DWARF tag: DW_TAG_variable (0x34) @ offset 0x12345\n"
      "  DWARF location (address range + DWARF expression):\n"
      "    [0x1000, 0x2000): push(0), register(rdx) + 1\n"
      "    [0x3000, 0x4000): push(1)\n";
  EXPECT_EQ(kExpectedPretty, out.AsString());
}

TEST(FormatSymbol, BaseType) {
  auto int32_type = MakeInt32Type();
  OutputBuffer out = FormatSymbol(nullptr, int32_type.get(), FormatSymbolOptions());
  const char kExpected[] =
      "Type: int32_t\n"
      "  DWARF tag: DW_TAG_base_type (0x24) (synthetic symbol)\n"
      "  Byte size: 4\n"
      "  DWARF base type: DW_ATE_signed (0x05)\n";
  EXPECT_EQ(kExpected, out.AsString());
}

TEST(FormatSymbol, Collection) {
  auto int32_type = MakeInt32Type();

  // Create a collection. The second member leaves a gap so we can test padding.
  auto coll = MakeCollectionType(DwarfTag::kStructureType, "MyStruct",
                                 {{"a", int32_type}, {"b", int32_type}});
  // This const cast is evil but it's cleaner to use the test utilities to create all the data
  // members and then reach in and make them the way we want (with extra padding) than duplicate all
  // of that logic.
  const_cast<DataMember*>(coll->data_members()[1].Get()->As<DataMember>())->set_member_location(8);
  coll->set_byte_size(12);

  // Say it inherits from this empty base class. Since it's empty, it will start at the same offset
  // as the first member.
  auto base_type = MakeCollectionType(DwarfTag::kStructureType, "BaseStruct", {});
  auto base_from = fxl::MakeRefCounted<InheritedFrom>(base_type, 0);

  // Virtual inheritance has no location but an expression to compute it. We use a dummy expression.
  auto virtual_base_type = MakeCollectionType(DwarfTag::kStructureType, "VirtualBase", {});
  auto virtual_base_from = fxl::MakeRefCounted<InheritedFrom>(virtual_base_type, DwarfExpr({0x01}));

  coll->set_inherited_from({LazySymbol(base_from), LazySymbol(virtual_base_from)});

  OutputBuffer out = FormatSymbol(nullptr, coll.get(), FormatSymbolOptions());
  const char kExpectedHeader[] =
      "Type: MyStruct\n"
      "  DWARF tag: DW_TAG_structure_type (0x13) (synthetic symbol)\n"
      "  Byte size: 12\n"
      "  Calling convention: DW_CC_normal\n";
  const char kMembers[] =  // Split off to allow re-use below.
      "  Members:\n"
      "       Offset Size Name         Type\n"
      "    <virtual>    0 <base class> VirtualBase\n"
      "            0    0 <base class> BaseStruct\n"
      "            0    4 a            int32_t\n"
      "            4    4              <padding>\n"
      "            8    4 b            int32_t\n";
  EXPECT_EQ(std::string(kExpectedHeader) + std::string(kMembers), out.AsString());

  // Re-test with a typedef. Typedefs of structures should show the members.
  auto coll_typedef = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kTypedef, coll);
  coll_typedef->set_assigned_name("MyTypedef");
  const char kTypedefHeader[] =
      "Type: MyTypedef\n"
      "  DWARF tag: DW_TAG_typedef (0x16) (synthetic symbol)\n"
      "  Byte size: 12\n"
      "  Underlying type: MyStruct\n";
  out = FormatSymbol(nullptr, coll_typedef.get(), FormatSymbolOptions());
  EXPECT_EQ(std::string(kTypedefHeader) + std::string(kMembers), out.AsString());
}

TEST(FormatSymbol, CallSite) {
  DwarfExpr value_expr({0x30,  // DW_OP_lit0
                        0x71,  // DW_OP_breg1
                        1});   // 1 (param for breg).
  auto param1 = fxl::MakeRefCounted<CallSiteParameter>(5, value_expr);
  auto param2 = fxl::MakeRefCounted<CallSiteParameter>(6, value_expr);

  auto call = fxl::MakeRefCounted<CallSite>(0x1000, std::vector<LazySymbol>{param1, param2});

  OutputBuffer out = FormatSymbol(nullptr, call.get(), FormatSymbolOptions());
  EXPECT_EQ(
      "Call Site\n"
      "  DWARF tag: DW_TAG_call_site (0x48) (synthetic symbol)\n"
      "  Return to: 0x1000\n"
      "  Parameters:\n"
      "    Call site parameter:\n"
      "      DWARF register #: 5\n"
      "      Value expression: push(0), dwarf_register(1) + 1\n"
      "    Call site parameter:\n"
      "      DWARF register #: 6\n"
      "      Value expression: push(0), dwarf_register(1) + 1\n",
      out.AsString());
}

}  // namespace zxdb
