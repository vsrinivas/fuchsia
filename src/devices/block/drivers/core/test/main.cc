// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/c/banjo.h>
#include <string.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

#include "manager.h"
#include "test/stub-block-device.h"

namespace {

TEST(ManagerTest, StartServer) {
  StubBlockDevice blkdev;
  ddk::BlockProtocolClient client(blkdev.proto());
  Manager manager;
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(nullptr, &client, &fifo));
  ASSERT_OK(manager.CloseFifoServer());
}

TEST(ManagerTest, AttachVmo) {
  StubBlockDevice blkdev;
  ddk::BlockProtocolClient client(blkdev.proto());
  Manager manager;
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(nullptr, &client, &fifo));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(8192, 0, &vmo));

  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  ASSERT_OK(manager.CloseFifoServer());
}

TEST(ManagerTest, CloseVMO) {
  StubBlockDevice blkdev;
  ddk::BlockProtocolClient client(blkdev.proto());
  Manager manager;
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(nullptr, &client, &fifo));
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(8192, 0, &vmo));
  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  // Request close VMO.
  block_fifo_request_t req = {
      .opcode = BLOCK_OP_CLOSE_VMO,
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
  std::vector<uint8_t> buf(zx_system_get_page_size());
  memset(buf.data(), 0x44, zx_system_get_page_size());
  zx_status_t status;
  for (size_t i = 0; i < size; i += zx_system_get_page_size()) {
    size_t remain = size - i;
    if (remain > zx_system_get_page_size()) {
      remain = zx_system_get_page_size();
    }
    if ((status = vmo->write(buf.data(), i, remain)) != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

TEST(ManagerTest, ReadSingleTest) {
  StubBlockDevice blkdev;
  ddk::BlockProtocolClient client(blkdev.proto());
  Manager manager;
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(nullptr, &client, &fifo));

  const size_t vmo_size = 8192;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));
  ASSERT_OK(FillVMO(zx::unowned_vmo(vmo), vmo_size));

  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  // Request close VMO.
  block_fifo_request_t req = {
      .opcode = BLOCK_OP_READ,
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

TEST(ManagerTest, ReadManyBlocksHasOneResponse) {
  StubBlockDevice blkdev;
  // Restrict max_transfer_size so that the server has to split up our request.
  block_info_t block_info = {
      .block_count = kBlockCount, .block_size = kBlockSize, .max_transfer_size = kBlockSize};
  blkdev.SetInfo(&block_info);
  ddk::BlockProtocolClient client(blkdev.proto());
  Manager manager;
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(nullptr, &client, &fifo));

  const size_t vmo_size = 8192;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));
  ASSERT_OK(FillVMO(zx::unowned_vmo(vmo), vmo_size));

  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  block_fifo_request_t reqs[2] = {
      {
          .opcode = BLOCK_OP_READ,
          .reqid = 0x100,
          .group = 0,
          .vmoid = vmoid,
          .length = 4,
          .vmo_offset = 0,
          .dev_offset = 0,
      },
      {
          .opcode = BLOCK_OP_READ,
          .reqid = 0x101,
          .group = 0,
          .vmoid = vmoid,
          .length = 1,
          .vmo_offset = 0,
          .dev_offset = 0,
      },
  };

  // Write requests.
  size_t actual_count = 0;
  ASSERT_OK(fifo.write(sizeof(reqs[0]), reqs, 2, &actual_count));
  ASSERT_EQ(actual_count, 2);

  // Wait for first response.
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(fifo.get(), ZX_FIFO_READABLE, ZX_TIME_INFINITE, &observed));

  block_fifo_response_t res;
  ASSERT_OK(fifo.read(sizeof(res), &res, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);
  ASSERT_OK(res.status);
  ASSERT_EQ(reqs[0].reqid, res.reqid);
  ASSERT_EQ(res.count, 1);

  // Wait for second response.
  ASSERT_OK(zx_object_wait_one(fifo.get(), ZX_FIFO_READABLE, ZX_TIME_INFINITE, &observed));

  ASSERT_OK(fifo.read(sizeof(res), &res, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);
  ASSERT_OK(res.status);
  ASSERT_EQ(reqs[1].reqid, res.reqid);
  ASSERT_EQ(res.count, 1);

  ASSERT_OK(manager.CloseFifoServer());
}

TEST(ManagerTest, TestLargeGroupedTransaction) {
  StubBlockDevice blkdev;
  // Restrict max_transfer_size so that the server has to split up our request.
  block_info_t block_info = {
      .block_count = kBlockCount, .block_size = kBlockSize, .max_transfer_size = kBlockSize};
  blkdev.SetInfo(&block_info);
  ddk::BlockProtocolClient client(blkdev.proto());
  Manager manager;
  zx::fifo fifo;
  ASSERT_OK(manager.StartServer(nullptr, &client, &fifo));

  const size_t vmo_size = 8192;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));
  ASSERT_OK(FillVMO(zx::unowned_vmo(vmo), vmo_size));

  vmoid_t vmoid;
  ASSERT_OK(manager.AttachVmo(std::move(vmo), &vmoid));

  block_fifo_request_t reqs[2] = {
      {
          .opcode = BLOCK_OP_READ | BLOCK_GROUP_ITEM,
          .reqid = 0x101,
          .group = 0,
          .vmoid = vmoid,
          .length = 4,
          .vmo_offset = 0,
          .dev_offset = 0,
      },
      {
          .opcode = BLOCK_OP_READ | BLOCK_GROUP_ITEM | BLOCK_GROUP_LAST,
          .reqid = 0x101,
          .group = 0,
          .vmoid = vmoid,
          .length = 1,
          .vmo_offset = 0,
          .dev_offset = 0,
      },
  };

  // Write requests.
  size_t actual_count = 0;
  ASSERT_OK(fifo.write(sizeof(reqs[0]), reqs, 2, &actual_count));
  ASSERT_EQ(actual_count, 2);

  // Wait for first response.
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(fifo.get(), ZX_FIFO_READABLE, ZX_TIME_INFINITE, &observed));

  block_fifo_response_t res;
  ASSERT_OK(fifo.read(sizeof(res), &res, 1, &actual_count));
  ASSERT_EQ(actual_count, 1);
  ASSERT_OK(res.status);
  ASSERT_EQ(reqs[0].reqid, res.reqid);
  ASSERT_EQ(res.count, 2);
  ASSERT_EQ(res.group, 0);
}

}  // namespace
