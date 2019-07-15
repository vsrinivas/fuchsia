// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_function.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

namespace zxdb {

TEST(FormatFunction, Regular) {
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("Function");

  // Function with no parameters.
  EXPECT_EQ("Function()", FormatFunctionName(function.get(), false).AsString());
  EXPECT_EQ("Function()", FormatFunctionName(function.get(), true).AsString());

  // Add two parameters.
  auto int32_type = MakeInt32Type();
  auto param_value = MakeVariableForTest("value", int32_type, 0x100, 0x200, std::vector<uint8_t>());
  auto param_other =
      MakeVariableForTest("other_param", int32_type, 0x100, 0x200, std::vector<uint8_t>());
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  EXPECT_EQ("Function(…)", FormatFunctionName(function.get(), false).AsString());
  EXPECT_EQ("Function(int32_t, int32_t)", FormatFunctionName(function.get(), true).AsString());

  // Put in a namespace and add some templates. This needs a new function because the name will be
  // cached above.
  function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("Function<int>");
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  auto ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name("ns");
  function->set_parent(LazySymbol(ns));

  EXPECT_EQ(
      "kNormal \"ns::\", "
      "kHeading \"Function\", "
      "kComment \"<int>(…)\"",
      FormatFunctionName(function.get(), false).GetDebugString());

  function->set_parent(LazySymbol());
}

TEST(FormatFunction, ClangLambda) {
  // Clang lambdas are anonymous classes with an "operator()" function.
  auto closure = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("operator()");
  function->set_parent(closure);

  EXPECT_EQ("λ()", FormatFunctionName(function.get(), false).AsString());
}

TEST(FormatFunction, GCCLambda) {
  // GCC lambdas are specially-named structs with an "operator()" function.
  auto closure = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  closure->set_assigned_name("<lambda()>");
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("operator()");
  function->set_parent(closure);

  EXPECT_EQ("λ()", FormatFunctionName(function.get(), false).AsString());
}

TEST(FormatFunction, RustClosure) {
  // Rust closures are named like:
  // "fuchsia_async::executor::{{impl}}::run_singlethreaded::{{closure}}<()>"
  // The function "assigned name" will be "{{closure}}<()>".

  // Make a function for the closure to be inside of.
  auto enclosing = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  enclosing->set_assigned_name("EnclosingFunction");

  auto closure = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  closure->set_assigned_name("{{closure}}<()>");
  closure->set_parent(enclosing);

  EXPECT_EQ("λ()", FormatFunctionName(closure.get(), false).AsString());
}

}  // namespace zxdb
