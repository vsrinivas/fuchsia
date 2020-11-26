// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_LAZY_READER_H_
#define SRC_STORAGE_MINFS_LAZY_READER_H_

#include <lib/zx/status.h>

#include <memory>
#include <vector>

#include <bitmap/rle-bitmap.h>
#include <fbl/algorithm.h>
#include <fs/transaction/buffered_operations_builder.h>
#include <fs/transaction/transaction_handler.h>

#include "src/storage/minfs/block_utils.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/writeback.h"

namespace minfs {

// LazyReader helps with lazily reading blocks in a file.
class LazyReader {
 public:
  LazyReader() = default;

  LazyReader(const LazyReader&) = delete;
  LazyReader& operator=(const LazyReader&) = delete;

  // An interface for reading blocks. An instance is passed to the Read method.
  class ReaderInterface {
   public:
    ReaderInterface() = default;

    // Not copyable or movable.
    ReaderInterface(ReaderInterface&) = delete;
    ReaderInterface& operator=(ReaderInterface&) = delete;

    virtual ~ReaderInterface() = default;

    // Queue a read for |range| blocks. Returns the number of blocks enqueued. The remainder will be
    // pased to a subsequent call.
    [[nodiscard]] virtual zx::status<uint64_t> Enqueue(BlockRange range) = 0;

    // Issues the queued reads and returns the result.
    [[nodiscard]] virtual zx_status_t RunRequests() = 0;

    virtual uint32_t BlockSize() const = 0;
  };

  // Reads |length| bytes at offset |offset| into the buffer (if it isn't already pressent) using
  // the provided ReaderInterface. The actual reads will be blocks and so |range| will be aligned to
  // the nearest block boundaries that encompass |range|.
  [[nodiscard]] zx_status_t Read(ByteRange range, ReaderInterface* reader);

  // Marks the given block range as loaded or not according to |set|.
  void SetLoaded(BlockRange range, bool set);

 private:
  bitmap::RleBitmap mapped_;
};

// An interface for mapping file blocks to device blocks.
class MapperInterface {
 public:
  MapperInterface() = default;

  // Not copyable or movable.
  MapperInterface(MapperInterface&) = delete;
  MapperInterface& operator=(MapperInterface&) = delete;

  virtual ~MapperInterface() = default;

  // Maps from file block to device block.
  [[nodiscard]] virtual zx::status<DeviceBlockRange> Map(BlockRange file_range) = 0;

  // Same as Map, but if |allocated| is non-null, blocks should be allocated if necessary and
  // |allocated| will be updated to indicate whether an allocation took place.
  [[nodiscard]] virtual zx::status<DeviceBlockRange> MapForWrite(PendingWork* transaction,
                                                                 BlockRange file_range,
                                                                 bool* allocated) = 0;
};

// MappedFileReader is a reader that can be used with LazyReader to read files that are mapped with
// an instance of MapperInterface.
class MappedFileReader : public LazyReader::ReaderInterface {
 public:
  MappedFileReader(fs::TransactionHandler* handler, MapperInterface* mapper,
                   storage::BlockBuffer* buffer)
      : handler_(*handler), mapper_(*mapper), buffer_(*buffer) {}

  uint32_t BlockSize() const override { return buffer_.BlockSize(); }

  zx::status<uint64_t> Enqueue(BlockRange range) override;

  [[nodiscard]] zx_status_t RunRequests() override {
    return handler_.RunRequests(builder_.TakeOperations());
  }

  MapperInterface& mapper() { return mapper_; }

 private:
  fs::TransactionHandler& handler_;
  MapperInterface& mapper_;
  storage::BlockBuffer& buffer_;
  fs::BufferedOperationsBuilder builder_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_LAZY_READER_H_
