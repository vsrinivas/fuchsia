// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <gtest/gtest.h>

#include "zircon/system/ulib/ftl/ftln/ftlnp.h"

namespace {

constexpr uint32_t kPageSize = 4096;
constexpr uint8_t kOobSize = 16;

TEST(FtlTest, IncompleteWriteWithValidity) {
  uint8_t spare[kOobSize];
  auto data = std::make_unique<uint8_t[]>(kPageSize);
  memset(spare, 0xff, kOobSize);
  memset(data.get(), 0xff, kPageSize);
  data.get()[0] = 0;
  FtlnSetSpareValidity(spare, data.get(), kPageSize);
  ASSERT_FALSE(FtlnIncompleteWrite(spare, data.get(), kPageSize));
}

TEST(FtlTest, IncompleteWriteWithBadValidity) {
  uint8_t spare[kOobSize];
  auto data = std::make_unique<uint8_t[]>(kPageSize);
  memset(spare, 0xff, kOobSize);
  memset(data.get(), 0xff, kPageSize);
  spare[14] = 0;
  ASSERT_TRUE(FtlnIncompleteWrite(spare, data.get(), kPageSize));
}

TEST(FtlTest, IncompleteWriteNoValidityBadWearCount) {
  uint8_t spare[kOobSize];
  auto data = std::make_unique<uint8_t[]>(kPageSize);
  memset(spare, 0xff, kOobSize);
  memset(data.get(), 0xff, kPageSize);
  ASSERT_TRUE(FtlnIncompleteWrite(spare, data.get(), kPageSize));
}

TEST(FtlTest, IncompleteWriteNoValidityGoodWearCount) {
  uint8_t spare[kOobSize];
  auto data = std::make_unique<uint8_t[]>(kPageSize);
  memset(spare, 0xff, kOobSize);
  memset(data.get(), 0xff, kPageSize);
  spare[10] = 0;
  ASSERT_FALSE(FtlnIncompleteWrite(spare, data.get(), kPageSize));
}

}  // namespace
