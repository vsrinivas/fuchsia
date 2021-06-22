// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../block.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/string.h>
#include <zxtest/zxtest.h>

#include "../usb-mass-storage.h"

namespace {

struct Context {
  ums::UmsBlockDevice* dev;
  fbl::String name;
  block_info_t info;
  block_op_t* op;
  zx_status_t status;
  ums::Transaction* txn;
};

class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceRemove(zx_device_t* dev) {
    Context* context = reinterpret_cast<Context*>(dev);
    context->dev->DdkRelease();
    fake_ddk::Bind::DeviceRemove(dev);
    return ZX_OK;
  }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) {
    *out = parent;
    Context* context = reinterpret_cast<Context*>(parent);
    context->name = fbl::String(args->name);
    return ZX_OK;
  }
};

static void BlockCallback(void* ctx, zx_status_t status, block_op_t* op) {
  Context* context = reinterpret_cast<Context*>(ctx);
  context->status = status;
  context->op = op;
}

TEST(UmsBlock, ConstructorTest) {
  Binder ddk;
  Context context;
  ums::UmsBlockDevice dev(reinterpret_cast<zx_device_t*>(&context), 5,
                          [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  ums::BlockDeviceParameters params = {};
  params.lun = 5;
  EXPECT_TRUE(params == dev.GetBlockDeviceParameters(),
              "Parameters must be set to user-provided values.");
  dev.Adopt();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

TEST(UmsBlock, AddTest) {
  Binder ddk;
  Context context;
  auto fake_zxdev = reinterpret_cast<zx_device_t*>(&context);
  ums::UmsBlockDevice dev(fake_zxdev, 5,
                          [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  ums::BlockDeviceParameters params = {};
  params.lun = 5;
  EXPECT_TRUE(params == dev.GetBlockDeviceParameters(),
              "Parameters must be set to user-provided values.");
  dev.Adopt();
  EXPECT_EQ(ZX_OK, dev.Add(), "Expected Add to succeed");
  dev.DdkAsyncRemove();
  ddk.WaitUntilRemove();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

TEST(UmsBlock, GetSizeTest) {
  Binder ddk;
  Context context;
  auto fake_zxdev = reinterpret_cast<zx_device_t*>(&context);
  ums::UmsBlockDevice dev(fake_zxdev, 5, [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  ums::BlockDeviceParameters params = {};
  params.lun = 5;
  dev.Adopt();
  EXPECT_TRUE(params == dev.GetBlockDeviceParameters(),
              "Parameters must be set to user-provided values.");
  EXPECT_EQ(ZX_OK, dev.Add(), "Expected Add to succeed");
  EXPECT_TRUE(fbl::String("lun-005") == context.name);
  params = dev.GetBlockDeviceParameters();
  params.block_size = 15;
  params.total_blocks = 700;
  context.info.block_size = params.block_size;
  context.info.block_count = params.total_blocks;
  dev.SetBlockDeviceParameters(params);
  EXPECT_EQ(params.block_size * params.total_blocks, dev.DdkGetSize());
  dev.DdkAsyncRemove();
  ddk.WaitUntilRemove();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

TEST(UmsBlock, NotSupportedTest) {
  Binder ddk;
  Context context;
  auto fake_zxdev = reinterpret_cast<zx_device_t*>(&context);
  ums::UmsBlockDevice dev(fake_zxdev, 5, [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  dev.Adopt();
  EXPECT_EQ(ZX_OK, dev.Add(), "Expected Add to succeed");
  EXPECT_TRUE(fbl::String("lun-005") == context.name);
  ums::Transaction txn;
  txn.op.command = BLOCK_OP_MASK;
  dev.BlockImplQueue(&txn.op, BlockCallback, &context);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, context.status);
  dev.DdkAsyncRemove();
  ddk.WaitUntilRemove();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

TEST(UmsBlock, ReadTest) {
  Binder ddk;
  Context context;
  auto fake_zxdev = reinterpret_cast<zx_device_t*>(&context);
  ums::UmsBlockDevice dev(fake_zxdev, 5, [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  dev.Adopt();
  EXPECT_EQ(ZX_OK, dev.Add(), "Expected Add to succeed");
  EXPECT_TRUE(fbl::String("lun-005") == context.name);
  ums::Transaction txn;
  txn.op.command = BLOCK_OP_READ;
  dev.BlockImplQueue(&txn.op, BlockCallback, &context);
  dev.DdkAsyncRemove();
  ddk.WaitUntilRemove();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

TEST(UmsBlock, WriteTest) {
  Binder ddk;
  Context context;
  auto fake_zxdev = reinterpret_cast<zx_device_t*>(&context);
  ums::UmsBlockDevice dev(fake_zxdev, 5, [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  dev.Adopt();
  EXPECT_EQ(ZX_OK, dev.Add(), "Expected Add to succeed");
  EXPECT_TRUE(fbl::String("lun-005") == context.name);
  ums::Transaction txn;
  txn.op.command = BLOCK_OP_WRITE;
  dev.BlockImplQueue(&txn.op, BlockCallback, &context);
  EXPECT_EQ(&txn, context.txn);
  dev.DdkAsyncRemove();
  ddk.WaitUntilRemove();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

TEST(UmsBlock, FlushTest) {
  Binder ddk;
  Context context;
  auto fake_zxdev = reinterpret_cast<zx_device_t*>(&context);
  ums::UmsBlockDevice dev(fake_zxdev, 5, [&](ums::Transaction* txn) { context.txn = txn; });
  context.dev = &dev;
  dev.Adopt();
  EXPECT_EQ(ZX_OK, dev.Add(), "Expected Add to succeed");
  EXPECT_TRUE(fbl::String("lun-005") == context.name);
  ums::Transaction txn;
  txn.op.command = BLOCK_OP_FLUSH;
  dev.BlockImplQueue(&txn.op, BlockCallback, &context);
  EXPECT_EQ(&txn, context.txn);
  dev.DdkAsyncRemove();
  ddk.WaitUntilRemove();
  EXPECT_TRUE(dev.Release(), "Expected to free the device");
}

}  // namespace
