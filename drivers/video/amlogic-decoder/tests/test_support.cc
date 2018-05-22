// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "test_support.h"

static zx_device_t* g_parent_device;

zx_device_t* TestSupport::parent_device() { return g_parent_device; }

void TestSupport::set_parent_device(zx_device_t* handle) {
  g_parent_device = handle;
}

void TestSupport::RunAllTests() {
  const int kArgc = 1;
  const char* argv[kArgc] = {"test_support"};
  testing::InitGoogleTest(const_cast<int*>(&kArgc), const_cast<char**>(argv));
  (void)RUN_ALL_TESTS();
}
