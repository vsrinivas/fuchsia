// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "driver_host_context.h"
#include "proxy_iostate.h"
#include "zx_device.h"

namespace {

TEST(DeviceApiTest, OpsNotImplemented) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, &dev));

  zx_protocol_device_t ops = {};
  dev->ops = &ops;

  EXPECT_EQ(device_get_protocol(dev.get(), 0, nullptr), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(device_get_size(dev.get()), 0);
}

uint64_t test_ctx = 0xabcdef;

zx_status_t test_get_protocol(void* ctx, uint32_t proto_id, void* out) {
  EXPECT_EQ(ctx, &test_ctx);
  EXPECT_EQ(proto_id, 42);
  uint8_t* data = static_cast<uint8_t*>(out);
  *data = 0xab;
  return ZX_OK;
}

zx_off_t test_get_size(void* ctx) {
  EXPECT_EQ(ctx, &test_ctx);
  return 42ul;
}

TEST(DeviceApiTest, GetProtocol) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, &dev));

  zx_protocol_device_t ops = {};
  ops.get_protocol = test_get_protocol;
  dev->ops = &ops;
  dev->ctx = &test_ctx;

  uint8_t out = 0;
  ASSERT_OK(device_get_protocol(dev.get(), 42, &out));
  EXPECT_EQ(out, 0xab);
}

TEST(DeviceApiTest, GetSize) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, &dev));

  zx_protocol_device_t ops = {};
  ops.get_size = test_get_size;
  dev->ops = &ops;
  dev->ctx = &test_ctx;

  ASSERT_EQ(device_get_size(dev.get()), 42ul);
}

}  // namespace
