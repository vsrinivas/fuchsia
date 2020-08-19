// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/code_generator/cpp_visitor.h"

#include <memory>
#include <sstream>

#include <gtest/gtest.h>

#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

TEST(CppVisitor, result) {
  CppVisitor visitor;
  auto val1 = std::make_unique<IntegerValue>(uint64_t(42));
  auto val2 = std::make_unique<IntegerValue>(uint64_t(17));

  std::unique_ptr<Uint64Type> type = std::make_unique<Uint64Type>();

  visitor.VisitValue(val1.get(), type.get());
  ASSERT_EQ(visitor.result()->value()->GetUint8Value(), 42);
  ASSERT_EQ(visitor.result()->name(), "unnamed_value");

  // The second call to visit will replace result_.

  visitor.VisitValue(val2.get(), type.get());
  ASSERT_EQ(visitor.result()->value()->GetUint8Value(), 17);
}

class CppVariableGenerateTest : public ::testing::Test {
 public:
  CppVariableGenerateTest() : printer_(PrettyPrinter(out_, WithoutColors, false, "", 100, false)) {
    CppVisitor visitor("my_variable_name");

    visitor.VisitValue(value_.get(), type_.get());
    var_ = visitor.result();
  }

  void SetUp() { out_.str(""); }

 protected:
  std::unique_ptr<IntegerValue> value_ = std::make_unique<IntegerValue>(uint64_t(42));
  std::unique_ptr<Uint64Type> type_ = std::make_unique<Uint64Type>();
  std::shared_ptr<CppVariable> var_;
  std::stringstream out_;
  PrettyPrinter printer_;
};

TEST_F(CppVariableGenerateTest, GenerateName) {
  var_->GenerateName(printer_);
  EXPECT_EQ(out_.str(), "my_variable_name");
}

TEST_F(CppVariableGenerateTest, GenerateType) {
  var_->GenerateType(printer_);
  EXPECT_EQ(out_.str(), "uint64_t");
}

TEST_F(CppVariableGenerateTest, GenerateTypeAndName) {
  var_->GenerateTypeAndName(printer_);
  EXPECT_EQ(out_.str(), "uint64_t my_variable_name");
}

TEST_F(CppVariableGenerateTest, GenerateLiteralValue) {
  var_->GenerateLiteralValue(printer_);
  EXPECT_EQ(out_.str(), "42");
}

TEST_F(CppVariableGenerateTest, GenerateDeclaration) {
  var_->GenerateDeclaration(printer_);
  EXPECT_EQ(out_.str(), "uint64_t my_variable_name;\n");
}

TEST_F(CppVariableGenerateTest, GenerateInitialization) {
  var_->GenerateInitialization(printer_);
  EXPECT_EQ(out_.str(), "uint64_t my_variable_name = 42;\n");
}

TEST_F(CppVariableGenerateTest, GenerateAssertStatement) {
  var_->GenerateAssertStatement(printer_);
  EXPECT_EQ(out_.str(),
            "uint64_t my_variable_name_expected = 42;\n"
            "ASSERT_EQ(my_variable_name, my_variable_name_expected);\n");
}

}  // namespace fidl_codec
