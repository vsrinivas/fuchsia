// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_name.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

namespace zxdb {

TEST(FormatName, Regular) {
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("Function");

  FormatFunctionNameOptions no_param_opts;
  no_param_opts.name.show_global_qual = false;
  no_param_opts.params = FormatFunctionNameOptions::kNoParams;

  FormatFunctionNameOptions elide_param_opts;
  elide_param_opts.name.show_global_qual = false;
  elide_param_opts.params = FormatFunctionNameOptions::kElideParams;

  FormatFunctionNameOptions type_param_opts;
  type_param_opts.name.show_global_qual = false;
  type_param_opts.params = FormatFunctionNameOptions::kParamTypes;

  // Function with no parameters.
  EXPECT_EQ("Function", FormatFunctionName(function.get(), no_param_opts).AsString());
  EXPECT_EQ("Function()", FormatFunctionName(function.get(), elide_param_opts).AsString());
  EXPECT_EQ("Function()", FormatFunctionName(function.get(), type_param_opts).AsString());

  // Add two parameters.
  auto int32_type = MakeInt32Type();
  auto param_value = MakeVariableForTest("value", int32_type, 0x100, 0x200, std::vector<uint8_t>());
  auto param_other =
      MakeVariableForTest("other_param", int32_type, 0x100, 0x200, std::vector<uint8_t>());
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  EXPECT_EQ("Function", FormatFunctionName(function.get(), no_param_opts).AsString());
  EXPECT_EQ("Function(…)", FormatFunctionName(function.get(), elide_param_opts).AsString());
  EXPECT_EQ("Function(int32_t, int32_t)",
            FormatFunctionName(function.get(), type_param_opts).AsString());

  // Put in a namespace and add some templates. This needs a new function because the name will be
  // cached above.
  function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("Function<int>");
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  auto ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name("ns");
  SymbolTestParentSetter function_parent(function, ns);

  elide_param_opts.name.bold_last = true;
  EXPECT_EQ(
      "kNormal \"ns::\", "
      "kHeading \"Function\", "
      "kComment \"<int>(…)\"",
      FormatFunctionName(function.get(), elide_param_opts).GetDebugString());
}

TEST(FormatName, ClangLambda) {
  // Clang lambdas are anonymous classes with an "operator()" function.
  auto closure = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("operator()");
  SymbolTestParentSetter function_parent(function, closure);

  EXPECT_EQ("λ()", FormatFunctionName(function.get(), FormatFunctionNameOptions()).AsString());
}

TEST(FormatName, GCCLambda) {
  // GCC lambdas are specially-named structs with an "operator()" function.
  auto closure = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  closure->set_assigned_name("<lambda()>");
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("operator()");
  SymbolTestParentSetter function_parent(function, closure);

  EXPECT_EQ("λ()", FormatFunctionName(function.get(), FormatFunctionNameOptions()).AsString());
}

TEST(FormatName, RustClosure) {
  // Rust closures are named like:
  // "fuchsia_async::executor::{{impl}}::run_singlethreaded::{{closure}}<()>"
  // The function "assigned name" will be "{{closure}}<()>".

  // Make a function for the closure to be inside of.
  auto enclosing = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  enclosing->set_assigned_name("EnclosingFunction");

  auto closure = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  closure->set_assigned_name("{{closure}}<()>");
  SymbolTestParentSetter closure_parent(closure, enclosing);

  EXPECT_EQ("λ()", FormatFunctionName(closure.get(), FormatFunctionNameOptions()).AsString());
}

TEST(FormatName, FormatIdentifier) {
  FormatIdentifierOptions global_opts;
  global_opts.show_global_qual = true;
  FormatIdentifierOptions no_global_opts;
  no_global_opts.show_global_qual = false;

  // Regular name with no global qualification.
  OutputBuffer output = FormatIdentifier(Identifier("ThisIsAName"), global_opts);
  EXPECT_EQ("kNormal \"ThisIsAName\"", output.GetDebugString());
  output = FormatIdentifier(Identifier("ThisIsAName"), no_global_opts);
  EXPECT_EQ("kNormal \"ThisIsAName\"", output.GetDebugString());

  // Regular name with bolding.
  FormatIdentifierOptions bold_global_opts;
  bold_global_opts.show_global_qual = true;
  bold_global_opts.bold_last = true;
  output = FormatIdentifier(Identifier("ThisIsAName"), bold_global_opts);
  EXPECT_EQ("kHeading \"ThisIsAName\"", output.GetDebugString());

  // Hierarchical name.
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier("::Foo<int, char*>::Bar<>", &ident);
  ASSERT_FALSE(err.has_error());
  EXPECT_EQ(
      "kNormal \"::Foo\", "
      "kComment \"<int, char*>\", "
      "kNormal \"::\", "
      "kHeading \"Bar\", "
      "kComment \"<>\"",
      FormatIdentifier(ident, bold_global_opts).GetDebugString());

  // Hide global qualification.
  FormatIdentifierOptions bold_no_global_opts;
  bold_no_global_opts.show_global_qual = true;
  bold_no_global_opts.bold_last = true;
  EXPECT_EQ("::Foo<int, char*>::Bar<>", FormatIdentifier(ident, bold_no_global_opts).AsString());

  // With template eliding.
  FormatIdentifierOptions elide_opts = bold_no_global_opts;
  elide_opts.elide_templates = true;
  EXPECT_EQ("::Foo<…>::Bar<>", FormatIdentifier(ident, elide_opts).AsString());

  // With an anonymous namespace.
  ParsedIdentifier anon(ParsedIdentifierComponent(""));
  anon.AppendComponent(ParsedIdentifierComponent("Function"));
  EXPECT_EQ(
      "kComment \"$anon\", "
      "kNormal \"::\", "
      "kHeading \"Function\"",
      FormatIdentifier(anon, bold_no_global_opts).GetDebugString());
}

}  // namespace zxdb
