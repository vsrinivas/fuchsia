// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "lib/fostr/fidl_types.h"

#include "gtest/gtest.h"

namespace fostr {
namespace {

// Tests fidl::Array formatting.
TEST(FidlTypes, Array) {
  std::ostringstream os;
  fidl::Array<std::string, 2> utensil_array;
  utensil_array[0] = "knife";
  utensil_array[1] = "spork";

  os << Indent << "utensil:" << utensil_array;

  EXPECT_EQ(
      "utensil:"
      "\n    [0] knife"
      "\n    [1] spork",
      os.str());
}

// Tests fidl::VectorPtr formatting.
TEST(FidlTypes, VectorPtr) {
  std::ostringstream os;
  fidl::VectorPtr<std::string> empty_vector;
  fidl::VectorPtr<std::string> utensil_vector;
  utensil_vector.push_back("knife");
  utensil_vector.push_back("spork");

  os << fostr::Indent << "empty:" << empty_vector
     << ", utensil:" << utensil_vector;

  EXPECT_EQ(
      "empty:<empty>, utensil:"
      "\n    [0] knife"
      "\n    [1] spork",
      os.str());
}

}  // namespace
}  // namespace fostr
