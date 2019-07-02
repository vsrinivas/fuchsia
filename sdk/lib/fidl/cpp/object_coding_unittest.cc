// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <lib/fidl/cpp/object_coding.h>

#include <utility>

#include "gtest/gtest.h"

namespace fidl {
namespace {

TEST(EncodeObject, Struct) {
  fidl::test::misc::Int64Struct s;
  s.x = 123;
  std::vector<uint8_t> data;
  const char* err_msg;
  EXPECT_EQ(ZX_OK, EncodeObject(&s, &data, &err_msg)) << err_msg;
  fidl::test::misc::Int64Struct t;
  EXPECT_EQ(ZX_OK, DecodeObject(data.data(), data.size(), &t, &err_msg)) << err_msg;
  EXPECT_EQ(s.x, 123);
  EXPECT_EQ(t.x, 123);
}

}  // namespace
}  // namespace fidl
