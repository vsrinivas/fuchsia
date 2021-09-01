// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <gtest/gtest.h>

#include "zircon/system/ulib/ftl/ftln/ftlnp.h"

namespace {

constexpr uint8_t kOobSize = 16;

TEST(FtlTest, IncompleteWriteWithValidity) {
  uint8_t spare[kOobSize];
  memset(spare, 0xff, kOobSize);
  FtlnSetSpareValidity(spare);
  ASSERT_FALSE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, IncompleteWriteWithBadValidity) {
  uint8_t spare[kOobSize];
  memset(spare, 0xff, kOobSize);
  spare[14] = 0;
  ASSERT_TRUE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, IncompleteWriteNoValidityBadWearCount) {
  uint8_t spare[kOobSize];
  memset(spare, 0xff, kOobSize);
  ASSERT_TRUE(FtlnIncompleteWrite(spare));
}

TEST(FtlTest, IncompleteWriteNoValidityGoodWearCount) {
  uint8_t spare[kOobSize];
  memset(spare, 0xff, kOobSize);
  spare[10] = 0;
  ASSERT_FALSE(FtlnIncompleteWrite(spare));
}

}  // namespace
