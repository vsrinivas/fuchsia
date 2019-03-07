// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "garnet/bin/zxdb/expr/type_parser.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(TypeParser, RoundTrip) {
  // Test round-trip input/output for normal type names.
  const char* kGoodTypes[] = {"int", "char*", "void*"};

  for (const char* type_name : kGoodTypes) {
    fxl::RefPtr<Type> type;
    Err err = StringToType(type_name, &type);
    ASSERT_FALSE(err.has_error())
        << "Can't parse \"" << type_name << "\": " << err.msg();
    EXPECT_EQ(type_name, type->GetFullName());
  }
}

}  // namespace zxdb
