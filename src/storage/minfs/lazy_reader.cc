// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/lazy_reader.h"

namespace minfs {

zx::status<> LazyReader::Read(ByteRange range, ReaderInterface* reader) {
  if (range.Length() == 0)
    return zx::ok();

  // Find the first block that isn't loaded.
  const range::Range block_range = BytesToBlocks(range, reader->BlockSize());
  uint64_t block = block_range.Start();
  if (mapped_.GetOne(block)) {
    mapped_.Find(false, block + 1, block_range.End(), 1, &block);
  }

  // Loop through all unloaded block ranges and enqueue reads for them.
  while (block < block_range.End()) {
    uint64_t end;
    mapped_.Find(true, block + 1, block_range.End(), 1, &end);
    auto status = EnumerateBlocks(BlockRange(block, end),
                                  [&](BlockRange range) { return reader->Enqueue(range); });
    if (status.is_error())
      return status.take_error();
    mapped_.Find(false, end + 1, block_range.End(), 1, &block);
  }

  // Issue and wait for the reads to complete.
  if (auto status = reader->RunRequests(); status.is_error())
    return status.take_error();

  // Mark the whole range as loaded.
  mapped_.Set(block_range.Start(), block_range.End());
  return zx::ok();
}

void LazyReader::SetLoaded(BlockRange range, bool set) {
  if (set) {
    mapped_.Set(range.Start(), range.End());
  } else {
    mapped_.Clear(range.Start(), range.End());
  }
}

zx::status<uint64_t> MappedFileReader::Enqueue(BlockRange range) {
  zx::status<DeviceBlockRange> status = mapper_.Map(range);
  if (status.is_error())
    return status.take_error();
  const DeviceBlockRange device_range = status.value();
  if (device_range.IsMapped()) {
    builder_.Add(
        storage::Operation{
            .type = storage::OperationType::kRead,
            .vmo_offset = range.Start(),
            .dev_offset = device_range.block(),
            .length = device_range.count(),
        },
        &buffer_);
  } else {
    // This probably isn't necessary because the blocks should already be clean, but it's safe.
    buffer_.Zero(range.Start(), device_range.count());
  }
  return zx::ok(device_range.count());
}

}  // namespace minfs
