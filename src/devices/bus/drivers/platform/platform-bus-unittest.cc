// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/bus/drivers/platform/platform-bus.h"

#include <lib/fake-bti/bti.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

namespace {

static size_t g_bti_created = 0;

TEST(PlatformBusTest, IommuGetBti) {
  g_bti_created = 0;
  platform_bus::PlatformBus pbus(nullptr, zx::channel());
  EXPECT_EQ(g_bti_created, 0);
  zx::bti bti;
  ASSERT_OK(pbus.IommuGetBti(0, 0, &bti));
  EXPECT_EQ(g_bti_created, 1);
  ASSERT_OK(pbus.IommuGetBti(0, 0, &bti));
  EXPECT_EQ(g_bti_created, 1);
  ASSERT_OK(pbus.IommuGetBti(0, 1, &bti));
  EXPECT_EQ(g_bti_created, 2);
}

}  // namespace

__EXPORT
zx_status_t zx_bti_create(zx_handle_t handle, uint32_t options, uint64_t bti_id, zx_handle_t* out) {
  g_bti_created++;
  return fake_bti_create(out);
}
