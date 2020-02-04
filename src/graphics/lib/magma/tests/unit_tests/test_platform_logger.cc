// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <platform_logger.h>
#include <string.h>

#include <gtest/gtest.h>

TEST(PlatformLogger, LogMacro) {
  // Assumes the logger has already been initialized.
  ASSERT_TRUE(magma::PlatformLogger::IsInitialized());
  MAGMA_LOG(INFO, "%s %s", "Hello", "world!");
}
