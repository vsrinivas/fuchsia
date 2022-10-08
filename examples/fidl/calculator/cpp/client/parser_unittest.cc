// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "examples/fidl/calculator/cpp/client/calc_parser.h"

TEST(CalcTest, TestBasic) {
  calc::Expression myTester(1, calc::Operator::Divide, 2);
  ASSERT_EQ(calc::Operator::Divide, myTester.GetOperator());
  ASSERT_EQ(1, myTester.GetLeft());
  ASSERT_EQ(2, myTester.GetRight());
}

TEST(CalcTest, Parse1) {
  calc::Expression myExpression("3.3 + 4.0");
  ASSERT_EQ(calc::Operator::Add, myExpression.GetOperator());
  ASSERT_EQ(3.3, myExpression.GetLeft());
  ASSERT_EQ(4.0, myExpression.GetRight());
}

TEST(CalcTest, Parse2) {
  calc::Expression myExpression("94.0043 / 332.33");
  ASSERT_EQ(calc::Operator::Divide, myExpression.GetOperator());
  ASSERT_EQ(94.0043, myExpression.GetLeft());
  ASSERT_EQ(332.33, myExpression.GetRight());
}

TEST(CalcTest, Parse3) {
  calc::Expression myExpression("-20043 ^ -32.33");
  ASSERT_EQ(calc::Operator::Pow, myExpression.GetOperator());
  ASSERT_EQ(-20043, myExpression.GetLeft());
  ASSERT_EQ(-32.33, myExpression.GetRight());
}

TEST(CalcTest, Parse4) {
  calc::Expression myExpression(".0043 - -0.3343");
  ASSERT_EQ(calc::Operator::Subtract, myExpression.GetOperator());
  ASSERT_EQ(.0043, myExpression.GetLeft());
  ASSERT_EQ(-0.3343, myExpression.GetRight());
}

TEST(CalcTest, Parse5) {
  calc::Expression myExpression(".0043 asdf -0.3343");
  ASSERT_EQ(calc::Operator::PlaceHolderError, myExpression.GetOperator());
  ASSERT_EQ(0, myExpression.GetLeft());
  ASSERT_EQ(0, myExpression.GetRight());

  calc::Expression myExpression2(" ");
  ASSERT_EQ(calc::Operator::PlaceHolderError, myExpression2.GetOperator());
  ASSERT_EQ(0, myExpression2.GetLeft());
  ASSERT_EQ(0, myExpression2.GetRight());
}
