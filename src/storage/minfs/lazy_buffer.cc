// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lazy_buffer.h"

namespace minfs {

zx::status<std::unique_ptr<LazyBuffer>> LazyBuffer::Create(Bcache* bcache, const char* name,
                                                           uint32_t block_size) {
  std::unique_ptr<LazyBuffer> buffer(new LazyBuffer(block_size));
  zx_status_t status = buffer->buffer_.Attach(name, bcache);
  if (status != ZX_OK)
    return zx::error(status);
  return zx::ok(std::move(buffer));
}

void LazyBuffer::Shrink(size_t block_count) {
  lazy_reader_.SetLoaded(BlockRange(block_count, std::numeric_limits<uint64_t>::max()), false);
  // ResizeableVmoBuffer has a minimum block size of 1.
  if (block_count < 1)
    block_count = 1;
  if (block_count < buffer_.capacity()) {
    zx_status_t status = buffer_.Shrink(block_count);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
}

zx_status_t LazyBuffer::Read(ByteRange range, Reader* reader) {
  if (range.Length() == 0)
    return ZX_OK;
  uint64_t required_blocks = BytesToBlocks(range, buffer_.BlockSize()).End();
  if (required_blocks > buffer_.capacity()) {
    zx_status_t status = Grow(required_blocks);
    if (status != ZX_OK)
      return status;
  }
  return lazy_reader_.Read(range, reader);
}

zx_status_t LazyBuffer::Flush(PendingWork* transaction, MapperInterface* mapper,
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
        zx_status_t status = writer(
            &buffer_, BlockRange(range.Start(), range.Start() + device_range.value().count()),
            device_range.value().block());
        if (status != ZX_OK)
          return zx::error(status);
        return zx::ok(device_range.value().count());
      });
}

}  // namespace minfs
