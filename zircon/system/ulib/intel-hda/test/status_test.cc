// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda/utils/status.h"

#include <zircon/errors.h>

#include <string>

#include <zxtest/zxtest.h>

namespace audio::intel_hda {
namespace {

TEST(Status, OkStatus) {
  Status s{};
  EXPECT_TRUE(s.ok());
  EXPECT_OK(s.code());
}

TEST(Status, ErrorStatus) {
  Status s{ZX_ERR_ACCESS_DENIED};
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ZX_ERR_ACCESS_DENIED);
}

TEST(Status, EmptyMessage) {
  Status s{};
  EXPECT_EQ(s.message(), "");
}

TEST(Status, ConstCharMessage) {
  Status s{ZX_ERR_ACCESS_DENIED, "Message"};
  EXPECT_EQ(s.message(), "Message");
}

TEST(Status, StringMessage) {
  Status s{ZX_ERR_ACCESS_DENIED, std::string("Message")};
  EXPECT_EQ(s.message(), "Message");
}

TEST(Status, ToString) {
  Status s1{};
  EXPECT_EQ(s1.ToString(), "ZX_OK");

  Status s2{ZX_ERR_ACCESS_DENIED};
  EXPECT_EQ(s2.ToString(), "ZX_ERR_ACCESS_DENIED");

  Status s3{ZX_ERR_ACCESS_DENIED, "Message"};
  EXPECT_EQ(s3.ToString(), "Message (ZX_ERR_ACCESS_DENIED)");
}

TEST(PrependMessage, NoMessage) {
  EXPECT_EQ(PrependMessage("prefix", Status()).ToString(), "prefix: ZX_OK (ZX_OK)");
}

TEST(PrependMessage, WithMessage) {
  EXPECT_EQ(PrependMessage("prefix", Status(ZX_ERR_ACCESS_DENIED, "Access denied")).ToString(),
            "prefix: Access denied (ZX_ERR_ACCESS_DENIED)");
}

}  // namespace
}  // namespace audio::intel_hda
