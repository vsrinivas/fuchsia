// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/string.h"

namespace fidl {
namespace {

TEST(StringPtr, Control) {
  StringPtr string;
  EXPECT_TRUE(string.is_null());
  EXPECT_FALSE(string);
  EXPECT_EQ(std::string(), string.get());

  string.reset("hello, world");
  EXPECT_FALSE(string.is_null());
  EXPECT_TRUE(string);
  EXPECT_EQ("hello, world", *string);
  EXPECT_EQ(12u, string->size());

  StringPtr other(std::move(string));
  EXPECT_EQ("hello, world", *other);
}

TEST(StringPtr, PutAt) {
  uint8_t buffer[1024];
  Builder builder(buffer, sizeof(buffer));

  StringPtr string("hello, world");

  StringView* view = builder.New<StringView>();
  EXPECT_EQ(nullptr, view->data());
  EXPECT_EQ(0u, view->size());
  EXPECT_TRUE(PutAt(&builder, view, &string));
  EXPECT_EQ("hello, world", *string);
  EXPECT_EQ(12u, view->size());
  EXPECT_EQ(0, memcmp(view->data(), string->data(), 12u));
}

}  // namespace
}  // namespace fidl
