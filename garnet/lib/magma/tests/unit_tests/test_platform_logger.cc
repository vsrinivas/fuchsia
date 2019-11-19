// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <platform_logger.h>

#include <gtest/gtest.h>

TEST(PlatformLogger, Helloworld) {
  MAGMA_LOG(INFO, "%s %s", "Hello", "world!");
  EXPECT_TRUE(magma::PlatformLogger::IsInitialized());
}
