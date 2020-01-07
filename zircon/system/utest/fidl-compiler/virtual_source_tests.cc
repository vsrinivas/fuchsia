// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "fidl/source_span.h"
#include "test_library.h"

namespace {

bool AddLine() {
  BEGIN_TEST;

  fidl::VirtualSourceFile file("imaginary-test-file");

  fidl::SourceSpan one = file.AddLine("one");
  fidl::SourceSpan two = file.AddLine("two");
  fidl::SourceSpan three = file.AddLine("three");

  EXPECT_STR_EQ(std::string(one.data()).c_str(), "one");
  EXPECT_STR_EQ(std::string(two.data()).c_str(), "two");
  EXPECT_STR_EQ(std::string(three.data()).c_str(), "three");

  END_TEST;
}

bool LineContaining() {
  BEGIN_TEST;

  fidl::VirtualSourceFile file("imaginary-test-file");

  file.AddLine("one");
  fidl::SourceSpan two = file.AddLine("two");
  file.AddLine("three");

  fidl::SourceFile::Position pos{};
  file.LineContaining(two.data(), &pos);
  EXPECT_EQ(pos.line, 2);
  EXPECT_EQ(pos.column, 1);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(virtual_source_tests)

RUN_TEST(AddLine)
RUN_TEST(LineContaining)

END_TEST_CASE(virtual_source_tests)
