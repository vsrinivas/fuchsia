// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <ddk/protocol/block.h>
#include <zxtest/zxtest.h>

#include "manager.h"

namespace {

constexpr uint32_t kBlockSize = 1024;
constexpr uint64_t kBlockCount = 4096;

// Block op size is currently an arbitrary value.
constexpr size_t kBlockOpSize = 4096;

block_info_t info{
    .block_count = kBlockCount,
    .block_size = kBlockSize,
    .max_transfer_size = 2048,
    .flags = 0,
    .reserved = 0,
};

void bop_query(void* ctx, block_info_t* out_info, size_t* out_block_op_size) {
  memcpy(out_info, &info, sizeof(info));
  *out_block_op_size = kBlockOpSize;
  return;
}

void bop_queue(void* ctx, block_op_t* bop, block_queue_callback callback, void* cookie) {
  callback(cookie, ZX_OK, bop);
  return;
}

block_protocol_ops_t block_ops{
    .query = bop_query,
    .queue = bop_queue,
};

block_protocol_t block_proto{
    .ops = &block_ops,
    .ctx = nullptr,
};

TEST(ManagerTest, StartServer) {
  Manager manager;
  ddk::BlockProtocolClient client(&block_proto);
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(&client, &fifo));
  ASSERT_OK(manager.CloseFifoServer());
}

TEST(ManagerTest, AttachVmo) {
  Manager manager;
  ddk::BlockProtocolClient client(&block_proto);
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(&client, &fifo));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(8192, 0, &vmo));

  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  ASSERT_OK(manager.CloseFifoServer());
}

TEST(ManagerTest, CloseVMO) {
  Manager manager;
  ddk::BlockProtocolClient client(&block_proto);
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(&client, &fifo));
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(8192, 0, &vmo));
  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  // Request close VMO.
  block_fifo_request_t req = {
      .opcode = BLOCKIO_CLOSE_VMO,
      .reqid = 0x100,
      .group = 0,
      .vmoid = vmoid,
      .length = 0,
      .vmo_offset = 0,
      .dev_offset = 0,
  };

  // Write request.
  size_t actual_count = 0;
  ASSERT_OK(fifo.write(sizeof(req), &req, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);

  // Wait for response.
  zx_signals_t observed;
  ASSERT_OK(fifo.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), &observed));

  block_fifo_response_t res;
  ASSERT_OK(fifo.read(sizeof(res), &res, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);
  ASSERT_OK(res.status);
  ASSERT_EQ(req.reqid, res.reqid);
  ASSERT_EQ(res.count, 1);

  ASSERT_OK(manager.CloseFifoServer());
}

zx_status_t FillVMO(zx::unowned_vmo vmo, size_t size) {
  uint8_t buf[PAGE_SIZE];
  memset(buf, 0x44, PAGE_SIZE);
  zx_status_t status;
  for (size_t i = 0; i < size; i += PAGE_SIZE) {
    size_t remain = size - i;
    if (remain > PAGE_SIZE) {
      remain = PAGE_SIZE;
    }
    if ((status = vmo->write(buf, i, remain)) != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

TEST(ManagerTest, ReadSingleTest) {
  Manager manager;
  ddk::BlockProtocolClient client(&block_proto);
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(&client, &fifo));

  const size_t vmo_size = 8192;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));
  ASSERT_OK(FillVMO(zx::unowned_vmo(vmo), vmo_size));

  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  // Request close VMO.
  block_fifo_request_t req = {
      .opcode = BLOCKIO_READ,
      .reqid = 0x100,
      .group = 0,
      .vmoid = vmoid,
      .length = 1,
      .vmo_offset = 0,
      .dev_offset = 0,
  };

  // Write request.
  size_t actual_count = 0;
  ASSERT_OK(fifo.write(sizeof(req), &req, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);

  // Wait for response.
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(fifo.get(), ZX_FIFO_READABLE, ZX_TIME_INFINITE, &observed));

  block_fifo_response_t res;
  ASSERT_OK(fifo.read(sizeof(res), &res, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);
  ASSERT_OK(res.status);
  ASSERT_EQ(req.reqid, res.reqid);
  ASSERT_EQ(res.count, 1);

  ASSERT_OK(manager.CloseFifoServer());
}

}  // namespace
