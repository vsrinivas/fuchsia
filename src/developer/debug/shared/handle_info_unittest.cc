// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/handle_info.h"

#include <gtest/gtest.h>

namespace debug_ipc {

namespace {

// Returns the vector of strings separated by "|" characters.
std::string ArrayToOr(const std::vector<std::string>& values) {
  std::string result;
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0)
      result += " | ";
    result += values[i];
  }

  return result;
}

std::string HandleRightsToString(uint32_t handle_rights) {
  return ArrayToOr(HandleRightsToStrings(handle_rights));
}

std::string VmoFlagsToString(uint32_t vmo_flags) { return ArrayToOr(VmoFlagsToStrings(vmo_flags)); }

}  // namespace

TEST(HandleInfo, HandleTypeToString) {
  EXPECT_EQ("ZX_OBJ_TYPE_NONE", HandleTypeToString(0u));
  EXPECT_EQ("ZX_OBJ_TYPE_SOCKET", HandleTypeToString(14u));
  std::string a = HandleTypeToString(9999);
  EXPECT_EQ("<unknown (9999)>", a);
}

TEST(HandleInfo, CachePolicyToString) {
  EXPECT_EQ("ZX_CACHE_POLICY_CACHED", CachePolicyToString(0));
  EXPECT_EQ("ZX_CACHE_POLICY_WRITE_COMBINING", CachePolicyToString(3));
  EXPECT_EQ("<unknown (789)>", CachePolicyToString(789));
}

TEST(HandleInfo, HandleRightsToStrings) {
  EXPECT_EQ("ZX_RIGHT_NONE", HandleRightsToString(0));
  EXPECT_EQ("ZX_RIGHT_DUPLICATE", HandleRightsToString(1));
  EXPECT_EQ("ZX_RIGHT_TRANSFER", HandleRightsToString(2));
  EXPECT_EQ("ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER", HandleRightsToString(3));
  EXPECT_EQ("ZX_RIGHT_DUPLICATE | <unknown (1 << 29)> | ZX_RIGHT_SAME_RIGHTS",
            HandleRightsToString(0b10100000'00000000'00000000'00000001));
}

TEST(HandleInfo, VmoFlagsToStrings) {
  EXPECT_EQ("ZX_INFO_VMO_TYPE_PHYSICAL", VmoFlagsToString(0));
  EXPECT_EQ("ZX_INFO_VMO_TYPE_PAGED", VmoFlagsToString(1));
  EXPECT_EQ(
      "ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE | ZX_INFO_VMO_IS_COW_CLONE | "
      "ZX_INFO_VMO_VIA_HANDLE | ZX_INFO_VMO_VIA_MAPPING | ZX_INFO_VMO_PAGER_BACKED | "
      "ZX_INFO_VMO_CONTIGUOUS",
      VmoFlagsToString(0b1111111));
}

}  // namespace debug_ipc
