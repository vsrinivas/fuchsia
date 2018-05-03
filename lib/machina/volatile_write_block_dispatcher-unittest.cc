// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/volatile_write_block_dispatcher.h"

#include "gtest/gtest.h"

namespace machina {
namespace {

constexpr size_t kDispatcherSize = 8 * 1024 * 1024;

// Read only dispatcher that returns blocks containing a single byte.
class StaticDispatcher : public BlockDispatcher {
 public:
  StaticDispatcher(size_t size, uint8_t value)
      : BlockDispatcher(size, true /* read-only */), value_(value) {}

  zx_status_t Flush() override { return ZX_OK; }

  zx_status_t Submit() override { return ZX_OK; }

  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override {
    memset(buf, value_, size);
    return ZX_OK;
  }

  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  uint8_t value_;
};

#define ASSERT_BLOCK_VALUE(ptr, val, size) \
  do {                                     \
    for (size_t i = 0; i < (size); ++i) {  \
      ASSERT_EQ((val), (ptr)[i]);          \
    }                                      \
  } while (0)

fbl::unique_ptr<VolatileWriteBlockDispatcher> CreateDispatcher() {
  fbl::unique_ptr<BlockDispatcher> dispatcher;
  zx_status_t status = BlockDispatcher::CreateVolatileWrapper(
      fbl::make_unique<StaticDispatcher>(kDispatcherSize, 0xab), &dispatcher);
  if (status != ZX_OK) {
    return nullptr;
  }
  return fbl::unique_ptr<VolatileWriteBlockDispatcher>(
      reinterpret_cast<VolatileWriteBlockDispatcher*>(dispatcher.release()));
}

TEST(BlockDispatcherTest, VolatileWriteBlock) {
  fbl::unique_ptr<VolatileWriteBlockDispatcher> dispatcher = CreateDispatcher();
  uint8_t block[512] = {};
  ASSERT_EQ(ZX_OK, dispatcher->Read(0, block, sizeof(block)));
  ASSERT_BLOCK_VALUE(block, 0xab, sizeof(block));

  uint8_t write_block[512];
  memset(write_block, 0xbe, sizeof(write_block));
  ASSERT_EQ(ZX_OK, dispatcher->Write(0, write_block, sizeof(write_block)));
  ASSERT_EQ(ZX_OK, dispatcher->Read(0, block, sizeof(block)));
  ASSERT_BLOCK_VALUE(block, 0xbe, sizeof(block));
}

TEST(BlockDispatcherTest, VolatileWriteBlockComplex) {
  fbl::unique_ptr<VolatileWriteBlockDispatcher> dispatcher = CreateDispatcher();

  // Write blocks 0 & 2, blocks 1 & 3 will hit the static dispatcher.
  uint8_t block[512];
  memset(block, 0xbe, sizeof(block));
  ASSERT_EQ(ZX_OK, dispatcher->Write(0, block, sizeof(block)));
  ASSERT_EQ(ZX_OK, dispatcher->Write(2 * sizeof(block), block, sizeof(block)));

  uint8_t result[2048] = {};
  ASSERT_EQ(ZX_OK, dispatcher->Read(0, result, sizeof(result)));
  ASSERT_BLOCK_VALUE(&result[0], 0xbe, 512);
  ASSERT_BLOCK_VALUE(&result[512], 0xab, 512);
  ASSERT_BLOCK_VALUE(&result[1024], 0xbe, 512);
  ASSERT_BLOCK_VALUE(&result[1536], 0xab, 512);
}

TEST(BlockDispatcherTest, VolatileWriteBadRequest) {
  fbl::unique_ptr<VolatileWriteBlockDispatcher> dispatcher = CreateDispatcher();

  uint8_t block[512] = {};
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, dispatcher->Read(1, block, sizeof(block)));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, dispatcher->Read(0, block, sizeof(block) - 1));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, dispatcher->Write(1, block, sizeof(block)));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            dispatcher->Write(0, block, sizeof(block) - 1));
}

}  // namespace
}  // namespace machina
