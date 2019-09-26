// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ge2d-on-device-test.h"

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

#include "../ge2d.h"

// Not sure if this test is needed anymore, since we have unit tests
// that run in the framework. But keeping this skeleton in place for
// now. if we don't need this, we should remove it.

namespace ge2d {

namespace {

#if 0
constexpr uint32_t kValidGe2dRevisionId = 0xCC23BEBB;
#endif
Ge2dDevice* g_ge2d_device;

}  // namespace

void Ge2dDeviceTester::SetUp() {}

void Ge2dDeviceTester::TearDown() {}

TEST(Ge2dDeviceTester, TestClkAndPower) {
#if 0
  EXPECT_EQ(kValidGe2dRevisionId, Id::Get().ReadFrom(g_ge2d_device->ge2d_mmio()).id());
#endif
}

zx_status_t Ge2dDeviceTester::RunTests(Ge2dDevice* ge2d) {
  g_ge2d_device = ge2d;
  const int kArgc = 1;
  const char* argv[kArgc] = {"ge2d-test"};
  return RUN_ALL_TESTS(kArgc, const_cast<char**>(argv));
}

}  // namespace ge2d
