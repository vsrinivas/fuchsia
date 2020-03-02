// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/developer/shell/interpreter/src/value.h"

TEST(InterpreterUnitTest, AssignValueToItself) {
  shell::interpreter::Value value;
  value.SetString("Test string.");
  ASSERT_EQ(value.GetString()->value(), "Test string.");
  value.Set(value);
  ASSERT_EQ(value.GetString()->value(), "Test string.");
}
