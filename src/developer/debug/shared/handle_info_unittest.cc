// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/handle_info.h"

#include <gtest/gtest.h>

namespace debug_ipc {

TEST(HandleInfo, HandleTypeToString) {
  EXPECT_EQ("ZX_OBJ_TYPE_NONE", HandleTypeToString(0u));
  EXPECT_EQ("ZX_OBJ_TYPE_SOCKET", HandleTypeToString(14u));
  std::string a = HandleTypeToString(9999);
  EXPECT_EQ("<unknown (9999)>", a);
}

TEST(HandleInfo, HandleRightsToString) {
  EXPECT_EQ("ZX_RIGHT_NONE", HandleRightsToString(0));
  EXPECT_EQ("ZX_RIGHT_DUPLICATE", HandleRightsToString(1));
  EXPECT_EQ("ZX_RIGHT_TRANSFER", HandleRightsToString(2));
  EXPECT_EQ("ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER", HandleRightsToString(3));
  EXPECT_EQ("ZX_RIGHT_DUPLICATE | <unknown (1 << 29)> | ZX_RIGHT_SAME_RIGHTS",
            HandleRightsToString(0b10100000'00000000'00000000'00000001));
}

}  // namespace debug_ipc
