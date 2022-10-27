// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.fs/cpp/wire.h>
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
  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("device-api-test", ctx.inspect().drivers(), &drv));

  auto driver = Driver::Create(drv.get());
  ASSERT_OK(driver.status_value());

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", *std::move(driver), &dev));

  zx_protocol_device_t ops = {};
  dev->set_ops(&ops);
  dev->vnode.reset();

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
  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("device-api-test", ctx.inspect().drivers(), &drv));

  auto driver = Driver::Create(drv.get());
  ASSERT_OK(driver.status_value());

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", *std::move(driver), &dev));

  zx_protocol_device_t ops = {};
  ops.get_protocol = test_get_protocol;
  dev->set_ops(&ops);
  dev->set_ctx(&test_ctx);
  dev->vnode.reset();

  uint8_t out = 0;
  ASSERT_OK(device_get_protocol(dev.get(), 42, &out));
  EXPECT_EQ(out, 0xab);
}

TEST(DeviceApiTest, GetSize) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("device-api-test", ctx.inspect().drivers(), &drv));

  auto driver = Driver::Create(drv.get());
  ASSERT_OK(driver.status_value());

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", *std::move(driver), &dev));

  zx_protocol_device_t ops = {};
  ops.get_size = test_get_size;
  dev->set_ops(&ops);
  dev->set_ctx(&test_ctx);
  dev->vnode.reset();

  ASSERT_EQ(device_get_size(dev.get()), 42ul);
}

TEST(DeviceApiTest, ReservedDeviceNames) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("device-api-test", ctx.inspect().drivers(), &drv));

  auto driver = Driver::Create(drv.get());
  ASSERT_OK(driver.status_value());

  fbl::RefPtr<zx_device> parent;
  ASSERT_OK(zx_device::Create(&ctx, "test", *std::move(driver), &parent));
  parent->set_ctx(&test_ctx);
  parent->vnode.reset();

  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.ops = &ops;

  zx_device_t* child;
  // Check that fuchsia_device::wire::kDeviceControllerName is reserved.
  args.name = fuchsia_device_fs::wire::kDeviceControllerName;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_add_from_driver(drv.get(), parent.get(), &args, &child));

  // Check that fuchsia_device::wire::kDeviceProtocolName is reserved.
  args.name = fuchsia_device_fs::wire::kDeviceProtocolName;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_add_from_driver(drv.get(), parent.get(), &args, &child));
}

}  // namespace
