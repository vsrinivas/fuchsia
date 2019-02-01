// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "lib/fostr/indent.h"

#include "gtest/gtest.h"

namespace fostr {
namespace {

// Tests basic indentation with the default 'indent by' value.
TEST(Indent, Basics) {
  std::ostringstream os;
  EXPECT_EQ(0, fostr::GetIdentLevel(os));
  os << "items:";
  os << fostr::Indent;
  EXPECT_EQ(1, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "item 1";
  os << fostr::NewLine << "item 2";
  os << fostr::Indent;
  EXPECT_EQ(2, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "item 2A";
  os << fostr::NewLine << "item 2B";
  os << fostr::Outdent;
  EXPECT_EQ(1, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "item 3";
  os << fostr::Outdent;
  EXPECT_EQ(0, fostr::GetIdentLevel(os));

  EXPECT_EQ(
      "items:"
      "\n    item 1"
      "\n    item 2"
      "\n        item 2A"
      "\n        item 2B"
      "\n    item 3",
      os.str());
}

// Tests indentation with an initial non-zero level.
TEST(Indent, InitialLevel) {
  std::ostringstream os;
  os << fostr::IdentLevel(2);
  EXPECT_EQ(2, fostr::GetIdentLevel(os));
  os << "items:";
  os << fostr::Indent;
  EXPECT_EQ(3, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "item 1";
  os << fostr::NewLine << "item 2";
  os << fostr::Indent;
  EXPECT_EQ(4, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "item 2A";
  os << fostr::NewLine << "item 2B";
  os << fostr::Outdent;
  EXPECT_EQ(3, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "item 3";
  os << fostr::Outdent;
  EXPECT_EQ(2, fostr::GetIdentLevel(os));

  EXPECT_EQ(
      "items:"
      "\n            item 1"
      "\n            item 2"
      "\n                item 2A"
      "\n                item 2B"
      "\n            item 3",
      os.str());
}

// Tests indentation with a specific 'indent by' value.
TEST(Indent, IndentBy) {
  std::ostringstream os;
  os << fostr::IdentBy(2);
  os << "items:";
  os << fostr::Indent;
  os << fostr::NewLine << "item 1";
  os << fostr::NewLine << "item 2";
  os << fostr::Indent;
  os << fostr::NewLine << "item 2A";
  os << fostr::NewLine << "item 2B";
  os << fostr::Outdent;
  os << fostr::NewLine << "item 3";
  os << fostr::Outdent;

  EXPECT_EQ(
      "items:"
      "\n  item 1"
      "\n  item 2"
      "\n    item 2A"
      "\n    item 2B"
      "\n  item 3",
      os.str());
}

// Tests that level underflow works as expected.
TEST(Indent, Underflow) {
  std::ostringstream os;
  os << fostr::Outdent;
  EXPECT_EQ(-1, fostr::GetIdentLevel(os));
  os << fostr::Outdent;
  EXPECT_EQ(-2, fostr::GetIdentLevel(os));
  os << fostr::Outdent;
  EXPECT_EQ(-3, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "should not be indented";
  os << fostr::Indent;
  EXPECT_EQ(-2, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "should not be indented";
  os << fostr::Indent;
  EXPECT_EQ(-1, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "should not be indented";
  os << fostr::Indent;
  EXPECT_EQ(0, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "should not be indented";
  os << fostr::Indent;
  EXPECT_EQ(1, fostr::GetIdentLevel(os));
  os << fostr::NewLine << "should be indented";

  EXPECT_EQ(
      "\nshould not be indented"
      "\nshould not be indented"
      "\nshould not be indented"
      "\nshould not be indented"
      "\n    should be indented",
      os.str());
}

}  // namespace
}  // namespace fostr
