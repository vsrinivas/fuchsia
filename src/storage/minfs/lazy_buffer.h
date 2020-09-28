// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_LAZY_BUFFER_H_
#define SRC_STORAGE_MINFS_LAZY_BUFFER_H_

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/buffer_view.h"
#include "src/storage/minfs/lazy_reader.h"
#include "src/storage/minfs/resizeable_buffer.h"

namespace minfs {

// A lazy buffer wraps a buffer with a lazy reader so that blocks in the buffer can be lazily
// read.
class LazyBuffer {
 public:
  // A callback responsible for writing the |range| blocks in buffer to |device_block|.
  using Writer = fit::function<zx_status_t(ResizeableBufferType* buffer, BlockRange range,
                                           DeviceBlock device_block)>;

  class Reader : public MappedFileReader {
   public:
    Reader(Bcache* bcache, MapperInterface* mapper, LazyBuffer* buffer)
        : MappedFileReader(bcache, mapper, &buffer->buffer()) {}
  };

  // Create an instance of LazyBuffer.
  [[nodiscard]] static zx::status<std::unique_ptr<LazyBuffer>> Create(Bcache* bcache,
                                                                      const char* name,
                                                                      uint32_t block_size);

  LazyBuffer(const LazyBuffer&) = delete;
  LazyBuffer& operator=(const LazyBuffer&) = delete;

  // Returns the size of the buffer in bytes.
  size_t size() const { return buffer_.capacity() * buffer_.BlockSize(); }

  ResizeableBufferType& buffer() { return buffer_; }

  // Users must call Detach before destruction.
  zx_status_t Detach(Bcache* bcache) { return buffer_.Detach(bcache); }

  [[nodiscard]] zx_status_t Grow(size_t block_count) { return buffer_.Grow(block_count); }

  // Shrink the buffer. Does nothing if buffer is smaller.
  void Shrink(size_t block_count);

  // Iterates through all the blocks in the view, maps from file offset to device offset using
  // |mapper| and then calls |writer| to actually write the data to the backing store.
  [[nodiscard]] zx_status_t Flush(PendingWork* transaction, MapperInterface* mapper,
                                  BaseBufferView* view, const Writer& writer);

  // Returns a read/write view for the given range. |flusher| will be called by the view if
  // modified. Implementations should call the Flush method above to flush the buffer, which
  // will do the mappings for you. For example:
  //
  // BufferView<uint64_t> view;
  // status = buffer->GetView(
  //     offset, count, &reader,
  //     [buffer, transaction](BaseBufferView* view) {
  //       Mapper mapper;
  //       return buffer->Flush(transaction, &mapper, view, writer);
  //     }, &view);
  //
  template <typename T>
  [[nodiscard]] zx::status<BufferView<T>> GetView(size_t index, size_t count, Reader* reader,
                                                  BaseBufferView::Flusher flusher) {
    const size_t offset = index * sizeof(T);
    zx_status_t status = Read(ByteRange(offset, offset + count * sizeof(T)), reader);
    if (status != ZX_OK)
      return zx::error(status);
    return zx::ok(
        BufferView<T>(BufferPtr::FromBlockBuffer(&buffer_), index, count, std::move(flusher)));
  }

  // Returns a read only view for the given range.
  template <typename T>
  [[nodiscard]] zx::status<BufferView<T>> GetView(size_t index, size_t count, Reader* reader) {
    return GetView<T>(index, count, reader, nullptr);
  }

 private:
  LazyBuffer(uint32_t block_size) : buffer_(block_size) {}

  // Calls lazy_reader_ to read |range| bytes (if not already present).
  zx_status_t Read(ByteRange range, Reader* reader);

  LazyReader lazy_reader_;
  ResizeableBufferType buffer_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_LAZY_BUFFER_H_
