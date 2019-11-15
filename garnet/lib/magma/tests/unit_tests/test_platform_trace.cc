// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "platform_trace_provider.h"
#include "platform_trace_provider_with_fdio.h"

#if MAGMA_ENABLE_TRACING

TEST(Tracing, Teardown) {
  auto platform_trace = magma::PlatformTraceProvider::CreateForTesting();
  EXPECT_TRUE(platform_trace);
  EXPECT_TRUE(magma::InitializeTraceProviderWithFdio(platform_trace.get()));
  EXPECT_TRUE(platform_trace->IsInitialized());
  platform_trace.reset();
}

#endif
