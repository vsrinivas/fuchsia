// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/async-loop/cpp/loop.h>
#include <zxtest/zxtest.h>

#include "proxy-iostate.h"
#include "zx-device.h"

namespace {

TEST(DeviceApiTest, OpsNotImplemented) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&dev));

  zx_protocol_device_t ops = {};
  dev->ops = &ops;

  EXPECT_EQ(device_get_protocol(dev.get(), 0, nullptr), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(device_get_size(dev.get()), 0);
  EXPECT_EQ(device_read(dev.get(), nullptr, 0, 0, nullptr), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(device_write(dev.get(), nullptr, 0, 0, nullptr), ZX_ERR_NOT_SUPPORTED);
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

zx_status_t test_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  EXPECT_EQ(ctx, &test_ctx);
  uint8_t* data = static_cast<uint8_t*>(buf);
  *data = 0xab;
  EXPECT_EQ(count, 1);
  EXPECT_EQ(off, 2);
  *actual = 3;
  return ZX_OK;
}

zx_status_t test_write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
  EXPECT_EQ(ctx, &test_ctx);
  const uint8_t* data = static_cast<const uint8_t*>(buf);
  EXPECT_EQ(*data, 0xab);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(off, 2);
  *actual = 3;
  return ZX_OK;
}

TEST(DeviceApiTest, GetProtocol) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&dev));

  zx_protocol_device_t ops = {};
  ops.get_protocol = test_get_protocol;
  dev->ops = &ops;
  dev->ctx = &test_ctx;

  uint8_t out = 0;
  ASSERT_OK(device_get_protocol(dev.get(), 42, &out));
  EXPECT_EQ(out, 0xab);
}

TEST(DeviceApiTest, GetSize) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&dev));

  zx_protocol_device_t ops = {};
  ops.get_size = test_get_size;
  dev->ops = &ops;
  dev->ctx = &test_ctx;

  ASSERT_EQ(device_get_size(dev.get()), 42ul);
}

TEST(DeviceApiTest, Read) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&dev));

  zx_protocol_device_t ops = {};
  ops.read = test_read;
  dev->ops = &ops;
  dev->ctx = &test_ctx;

  uint8_t buf = 0;
  size_t actual = 0;
  ASSERT_OK(device_read(dev.get(), &buf, 1, 2, &actual));
  EXPECT_EQ(buf, 0xab);
  EXPECT_EQ(actual, 3);
}

TEST(DeviceApiTest, Write) {
  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&dev));

  zx_protocol_device_t ops = {};
  ops.write = test_write;
  dev->ops = &ops;
  dev->ctx = &test_ctx;

  uint8_t buf = 0xab;
  size_t actual = 0;
  ASSERT_OK(device_write(dev.get(), &buf, 1, 2, &actual));
  EXPECT_EQ(actual, 3);
}

}  // namespace
