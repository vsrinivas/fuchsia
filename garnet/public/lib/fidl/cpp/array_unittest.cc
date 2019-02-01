// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/array.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/builder.h"

namespace fidl {
namespace {

TEST(Array, BuilderTest) {
  char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Builder builder(buffer, ZX_CHANNEL_MAX_MSG_BYTES);

  fidl::Array<int, 3>* view = builder.New<fidl::Array<int, 3>>();
  EXPECT_EQ(3u, view->size());

  (*view)[0] = 0;
  (*view)[1] = 1;
  (*view)[2] = 2;

  EXPECT_EQ(view->at(0), 0);
  EXPECT_EQ(view->at(1), 1);
  EXPECT_EQ(view->at(2), 2);

  int i = 0;
  for (auto& value : *view) {
    EXPECT_EQ(i, value);
    ++i;
  }

  EXPECT_NE(view->data(), nullptr);
}

}  // namespace
}  // namespace fidl
