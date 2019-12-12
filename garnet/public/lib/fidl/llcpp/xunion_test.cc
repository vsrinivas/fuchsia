// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <zircon/fidl.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fidl/llcpp/types/test/llcpp/fidl.h>

#include "gtest/gtest.h"

namespace llcpp_test = ::llcpp::fidl::llcpp::types::test;

TEST(XUnion, InitialTag) {
  llcpp_test::TestXUnion flexible_xunion;
  EXPECT_TRUE(flexible_xunion.has_invalid_tag());

  llcpp_test::TestStrictXUnion strict_xunion;
  EXPECT_TRUE(strict_xunion.has_invalid_tag());
}

TEST(XUnion, UnknownTagFlexible) {
  fidl_xunion_tag_t unknown_tag = 0x01020304;
  int32_t xunion_data = 0x0A0B0C0D;
  auto flexible_xunion = llcpp_test::TestXUnion::WithPrimitive(&xunion_data);

  // set to an unknown tag
  auto raw_bytes = reinterpret_cast<uint32_t*>(&flexible_xunion);
  raw_bytes[0] = unknown_tag;

  EXPECT_EQ(flexible_xunion.which(), llcpp_test::TestXUnion::Tag::kUnknown);
  int32_t unknown_data = *(static_cast<int32_t*>(flexible_xunion.unknownData()));
  EXPECT_EQ(unknown_data, xunion_data);
}

TEST(XUnion, UnknownTagStrict) {
  fidl_xunion_tag_t unknown_tag = 0x01020304;
  int32_t xunion_data = 0x0A0B0C0D;
  auto strict_xunion = llcpp_test::TestStrictXUnion::WithPrimitive(&xunion_data);

  // set to an unknown tag
  auto raw_bytes = reinterpret_cast<uint32_t*>(&strict_xunion);
  raw_bytes[0] = unknown_tag;

  EXPECT_EQ(static_cast<fidl_xunion_tag_t>(strict_xunion.which()), unknown_tag);
}
