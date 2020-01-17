// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gdc-on-device-test.h"

#include <ddk/debug.h>

#include "../gdc-regs.h"
#include "../gdc.h"

namespace gdc {

namespace {

constexpr uint32_t kValidGdcRevisionId = 0xCC23BEBB;
GdcDevice* g_gdc_device;

}  // namespace

void GdcDeviceTester::SetUp() {}

void GdcDeviceTester::TearDown() {}

TEST(GdcDeviceTester, TestClkAndPower) {
  EXPECT_EQ(kValidGdcRevisionId, Id::Get().ReadFrom(g_gdc_device->gdc_mmio()).id());
}

zx_status_t GdcDeviceTester::RunTests(GdcDevice* gdc) {
  g_gdc_device = gdc;
  const int kArgc = 1;
  const char* argv[kArgc] = {"gdc-test"};
  return RUN_ALL_TESTS(kArgc, const_cast<char**>(argv));
}

}  // namespace gdc
