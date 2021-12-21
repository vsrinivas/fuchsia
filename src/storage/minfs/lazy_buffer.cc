// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/lazy_buffer.h"

namespace minfs {

zx::status<std::unique_ptr<LazyBuffer>> LazyBuffer::Create(Bcache* bcache, const char* name,
                                                           uint32_t block_size) {
  std::unique_ptr<LazyBuffer> buffer(new LazyBuffer(block_size));
  auto status = buffer->buffer_.Attach(name, bcache);
  if (status.is_error())
    return status.take_error();
  return zx::ok(std::move(buffer));
}

void LazyBuffer::Shrink(size_t block_count) {
  lazy_reader_.SetLoaded(BlockRange(block_count, std::numeric_limits<uint64_t>::max()), false);
  // ResizeableVmoBuffer has a minimum block size of 1.
  if (block_count < 1)
    block_count = 1;
  if (block_count < buffer_.capacity()) {
    auto status = buffer_.Shrink(block_count);
    ZX_DEBUG_ASSERT(status.is_ok());
  }
}

zx::status<> LazyBuffer::Read(ByteRange range, Reader* reader) {
  if (range.Length() == 0)
    return zx::ok();
  uint64_t required_blocks = BytesToBlocks(range, buffer_.BlockSize()).End();
  if (required_blocks > buffer_.capacity()) {
    if (auto status = Grow(required_blocks); status.is_error())
      return status.take_error();
  }
  return lazy_reader_.Read(range, reader);
}

zx::status<> LazyBuffer::Flush(PendingWork* transaction, MapperInterface* mapper,
                               BaseBufferView* view, const Writer& writer) {
  // TODO(fxbug.dev/50606): If this or the transaction fails, this will leave memory in an
  // indeterminate state. For now, this is no worse than it has been for some time.
  view->set_dirty(false);
  return EnumerateBlocks(
      view->GetByteRange(), buffer_.BlockSize(),
      [this, transaction, mapper, &writer](BlockRange range) -> zx::status<uint64_t> {
        bool allocated;
        zx::status<DeviceBlockRange> device_range =
            mapper->MapForWrite(transaction, range, &allocated);
        if (device_range.is_error())
          return device_range.take_error();
        auto status = writer(
            &buffer_, BlockRange(range.Start(), range.Start() + device_range.value().count()),
            device_range.value().block());
        if (status.is_error())
          return status.take_error();
        return zx::ok(device_range.value().count());
      });
}

}  // namespace minfs
