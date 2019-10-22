// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/utils.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {
constexpr char kDefaultApiKey[] = "cobalt-default-api-key";

TEST(ReadApiKeyOrDefault, CheckNotEmpty) {
  auto api_key = cobalt::ReadApiKeyOrDefault();
  EXPECT_NE(api_key, "");

  // API key should be either the default key or 8 bytes long.
  if (api_key != kDefaultApiKey) {
    EXPECT_EQ(api_key.size(), 8);
  }
}
}  // namespace
