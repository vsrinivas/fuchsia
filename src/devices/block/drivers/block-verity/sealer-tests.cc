// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/task.h>
#include <lib/async/time.h>

#include <zxtest/zxtest.h>

#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/sealer.h"

namespace {

constexpr uint64_t kBlockCount = 8192;

class TestSealer;

typedef struct read_task_data {
  async_task_t task;
  TestSealer* sealer;
  uint64_t block_index;
} read_task_data_t;

class TestSealer : public block_verity::Sealer {
 public:
  TestSealer(async_dispatcher_t* dispatcher)
      : block_verity::Sealer(block_verity::Geometry(block_verity::kBlockSize,
                                                    block_verity::kHashOutputSize, kBlockCount)),
        dispatcher_(dispatcher) {}

  virtual ~TestSealer() = default;

  zx_status_t Seal() { return StartSealing(this, SealCompleteCallback); }

  static void AsyncReadHandler(async_dispatcher_t* async, async_task_t* task, zx_status_t status) {
    // Do stuff with task
    read_task_data_t* task_data = reinterpret_cast<read_task_data_t*>(task);

    // Trigger callback
    task_data->sealer->AsyncCompleteRead(task_data->block_index);

    // Free task
    free(task_data);
  }

  void AsyncCompleteRead(uint64_t block_index) {
    // Claim the read succeeded, and provide an empty block.
    uint8_t block[block_verity::kBlockSize];
    memset(block, 0, block_verity::kBlockSize);
    CompleteRead(ZX_OK, block);
  }

  static void SealCompleteCallback(void* cookie, zx_status_t status, const uint8_t* seal_buf,
                                   size_t seal_len) {
    auto sealer = static_cast<TestSealer*>(cookie);
    sealer->OnSealCompleted(status, seal_buf, seal_len);
  }

  // Stub functions to override the required pure virtual functions.
  virtual void RequestRead(uint64_t block_index) {
    // We need to set up an event loop and schedule reads as async tasks on that
    // event loop, because otherwise we'll blow out the stack by running the
    // entire logic of sealing on one ever-descending callstack which, without
    // heap allocation, would wind up taking at least 8126 reads * 4k block
    // buffer per read = 31MiB stack space, which is unreasonably large to
    // expect on the system stack.
    read_task_data_t* task = static_cast<read_task_data_t*>(calloc(1, sizeof(read_task_data_t)));
    task->task.handler = AsyncReadHandler;
    task->task.deadline = async_now(dispatcher_);
    task->sealer = this;
    task->block_index = block_index;
    ASSERT_OK(async_post_task(dispatcher_, reinterpret_cast<async_task_t*>(task)));
  }

  // Note: it's safe to do the write completions and flushes inline; there's only 65
  // integrity block writes for an 8192-block device, and only the superblock
  // write actually uses appreciable stack space.  So for simplicity we just
  // complete them inline, which means they all go on the callstack recursively,
  // but that's fine.

  virtual void WriteIntegrityBlock(block_verity::HashBlockAccumulator& hba, uint64_t block_index) {
    // Claim the write succeeded.
    CompleteIntegrityWrite(ZX_OK);
  }

  virtual void WriteSuperblock() {
    // Ask the sealer core to prepare a superblock into this buffer.
    uint8_t block[block_verity::kBlockSize];
    PrepareSuperblock(block);
    // Claim the write succeeded.
    CompleteSuperblockWrite(ZX_OK);
  }

  virtual void RequestFlush() {
    // Claim the flush succeeded.
    CompleteFlush(ZX_OK);
  }

  // Hook to allow tests to make assertions once seal callbacks are triggered.
  virtual void OnSealCompleted(zx_status_t status, const uint8_t* seal, size_t len) {
    finished_ = true;
    seal_status_ = status;
    if (status == ZX_OK) {
      ASSERT_EQ(len, block_verity::kHashOutputSize);
      memcpy(seal_, seal, block_verity::kHashOutputSize);
    }
  }

  // Accessor to get at the internal state of the Sealer
  State state() { return state_; }

  async_dispatcher_t* dispatcher_;
  bool finished_ = false;
  zx_status_t seal_status_;
  uint8_t seal_[block_verity::kHashOutputSize];
};

TEST(SealerTest, SucceedsBasic) {
  // The expected seal obtained for a 8126-block, all zeroes data section.  See
  // detailed derivaction in block-verity-tests.cc
  uint8_t expected_seal[32] = {0x79, 0x66, 0xa2, 0x81, 0x27, 0x55, 0xbc, 0x70, 0xba, 0x70, 0x58,
                               0xbe, 0x1f, 0xbb, 0xf1, 0xc4, 0xd8, 0x06, 0xf1, 0xd4, 0x0b, 0x16,
                               0x00, 0xaa, 0xc2, 0x96, 0x33, 0x32, 0xbf, 0x78, 0x1e, 0x28};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  TestSealer sealer(loop.dispatcher());
  ASSERT_OK(sealer.Seal());
  loop.RunUntilIdle();
  ASSERT_TRUE(sealer.finished_);
  ASSERT_OK(sealer.seal_status_);
  ASSERT_EQ(memcmp(sealer.seal_, expected_seal, 32), 0);
}

TEST(SealerTest, FailsOnReadFailure) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  class ReadFailSealer : public TestSealer {
   public:
    ReadFailSealer(async_dispatcher_t* dispatcher) : TestSealer(dispatcher) {}
    ~ReadFailSealer() = default;
    void RequestRead(uint64_t block_index) override {
      uint8_t block[block_verity::kBlockSize];
      memset(block, 0, block_verity::kBlockSize);
      // CompleteRead with not ZX_OK
      CompleteRead(ZX_ERR_IO, block);
    }
  };

  ReadFailSealer sealer(loop.dispatcher());
  ASSERT_OK(sealer.Seal());
  loop.RunUntilIdle();
  ASSERT_TRUE(sealer.finished_);
  ASSERT_EQ(sealer.seal_status_, ZX_ERR_IO);
  ASSERT_EQ(sealer.state(), block_verity::Sealer::Failed);
}

TEST(SealerTest, FailsOnIntegrityWriteFailure) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  class IntegrityWriteFailSealer : public TestSealer {
   public:
    IntegrityWriteFailSealer(async_dispatcher_t* dispatcher) : TestSealer(dispatcher) {}
    ~IntegrityWriteFailSealer() = default;
    void WriteIntegrityBlock(block_verity::HashBlockAccumulator& hba, uint64_t block) override {
      CompleteIntegrityWrite(ZX_ERR_IO);
    }
  };
  IntegrityWriteFailSealer sealer(loop.dispatcher());

  ASSERT_OK(sealer.Seal());
  loop.RunUntilIdle();
  ASSERT_TRUE(sealer.finished_);
  ASSERT_EQ(sealer.seal_status_, ZX_ERR_IO);
  ASSERT_EQ(sealer.state(), block_verity::Sealer::Failed);
}

TEST(SealerTest, FailsOnSuperblockFailure) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  class SuperblockWriteFailSealer : public TestSealer {
   public:
    SuperblockWriteFailSealer(async_dispatcher_t* dispatcher) : TestSealer(dispatcher) {}
    ~SuperblockWriteFailSealer() = default;
    void WriteSuperblock() override {
      uint8_t block[block_verity::kBlockSize];
      PrepareSuperblock(block);
      CompleteSuperblockWrite(ZX_ERR_IO);
    }
  };
  SuperblockWriteFailSealer sealer(loop.dispatcher());

  ASSERT_OK(sealer.Seal());
  loop.RunUntilIdle();
  ASSERT_TRUE(sealer.finished_);
  ASSERT_EQ(sealer.seal_status_, ZX_ERR_IO);
  ASSERT_EQ(sealer.state(), block_verity::Sealer::Failed);
}

TEST(SealerTest, FailsOnFlushFailure) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  class FlushFailSealer : public TestSealer {
   public:
    FlushFailSealer(async_dispatcher_t* dispatcher) : TestSealer(dispatcher) {}
    ~FlushFailSealer() = default;
    void RequestFlush() override { CompleteFlush(ZX_ERR_IO); }
  };
  FlushFailSealer sealer(loop.dispatcher());

  ASSERT_OK(sealer.Seal());
  loop.RunUntilIdle();
  ASSERT_TRUE(sealer.finished_);
  ASSERT_EQ(sealer.seal_status_, ZX_ERR_IO);
  ASSERT_EQ(sealer.state(), block_verity::Sealer::Failed);
}

}  // namespace
