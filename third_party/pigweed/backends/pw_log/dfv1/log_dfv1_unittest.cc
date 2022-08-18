// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string_view>

#include <gtest/gtest.h>

#include "pw_log_backend/log_backend.h"

TEST(LogDdkTest, BaseName) {
  EXPECT_EQ(pw_log_ddk::BaseName(nullptr), nullptr);
  EXPECT_TRUE(std::string_view(pw_log_ddk::BaseName("")).empty());
  EXPECT_EQ(pw_log_ddk::BaseName("main.cc"), std::string_view("main.cc"));
  EXPECT_EQ(pw_log_ddk::BaseName("/main.cc"), std::string_view("main.cc"));
  EXPECT_EQ(pw_log_ddk::BaseName("../foo/bar//main.cc"), std::string_view("main.cc"));
}
