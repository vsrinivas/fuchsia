// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TEST_UNIT_JOURNAL_INTEGRATION_FIXTURE_H_
#define SRC_STORAGE_MINFS_TEST_UNIT_JOURNAL_INTEGRATION_FIXTURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <cstdint>
#include <memory>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/minfs_private.h"

namespace minfs {

class JournalIntegrationFixture : public testing::Test {
 public:
  static constexpr uint64_t kBlockCount = 1 << 15;
  static constexpr uint32_t kBlockSize = 512;
  static constexpr uint32_t kDiskBlocksPerFsBlock = kMinfsBlockSize / kBlockSize;
  static constexpr uint64_t kSliceSize = 512 * 1024;
  static constexpr uint64_t kSliceCount = kBlockCount * kBlockSize / kSliceSize;

  JournalIntegrationFixture();

  // Performs the operation with no limits and updates write_count_.
  void SetUp() override;

  async_dispatcher_t* dispatcher() const { return vfs_loop_.dispatcher(); }

  // Returns the appropriate write count for the operation under test.
  uint64_t write_count() const { return write_count_; }

  // Returns a device which attempts to perform the operation, but has a limit
  // of |allowed_blocks_blocks| writable disk blocks.
  std::unique_ptr<Bcache> CutOffDevice(uint64_t allowed_blocks);

 protected:
  // To be overridden by subclasses to perform an operation.
  virtual void PerformOperation(Minfs* fs) = 0;

  // Records an appropriate write count that can be used to determine a suitable value for
  // CutOffDevice. PerformOperation may call this, or it will be called automatically by
  // CountWritesToPerformOperation.
  void RecordWriteCount(Minfs* fs);

 private:
  // Collects the number of write operations necessary to perform an operation.
  //
  // Reformats the provided |in_out_device|, which acts as both an input and output parameter.
  void CountWritesToPerformOperation(
      std::unique_ptr<block_client::FakeFVMBlockDevice>* in_out_device);

  // Performs a user-requested operation with a "write limit".
  //
  // See "CountWritesToPerformOperation" for a reasonable |write_count| value to set.
  void PerformOperationWithTransactionLimit(
      uint64_t write_count, std::unique_ptr<block_client::FakeFVMBlockDevice>* in_out_device);

  async::Loop vfs_loop_;

  // Disk block writes to perform the operation normally.
  uint64_t write_count_ = 0;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_TEST_UNIT_JOURNAL_INTEGRATION_FIXTURE_H_
