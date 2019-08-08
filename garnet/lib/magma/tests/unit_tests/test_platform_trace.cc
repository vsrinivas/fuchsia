// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "platform_trace.h"

#if MAGMA_ENABLE_TRACING

TEST(Tracing, Teardown) {
  auto platform_trace = magma::PlatformTrace::CreateForTesting();
  EXPECT_TRUE(platform_trace);
  EXPECT_TRUE(platform_trace->Initialize());
  platform_trace.reset();
}

#endif
