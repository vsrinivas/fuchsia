// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_SPARSE_READER_H_
#define FVM_SPARSE_READER_H_

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <lz4/lz4frame.h>

#include "fvm/fvm-sparse.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#define LZ4_MAX_BLOCK_SIZE 65536

namespace fvm {

namespace internal {
// Helper struct for providing information about the buffer state.
struct BufferInfo {
  // Offset into buffer where valid data begins
  size_t offset;

  // Actual size of data contained within buffer
  size_t size;
};

// Internal class for representing read and write buffers for the sparse image.
class Buffer {
 public:
  Buffer();
  Buffer(uint64_t offset, size_t size, uint64_t capacity);
  Buffer(const Buffer&) = delete;
  Buffer(Buffer&&);
  Buffer& operator=(const Buffer&) = delete;
  Buffer& operator=(Buffer&&);
  ~Buffer();

  // Write |length| bytes from |indata| into buffer.
  bool IsEmpty() const;

  // Writes |length| number of bytes from |data| into the buffer.
  void Write(uint8_t* data, size_t length);

  // Read up to |length| bytes from the buffer into |target|, setting |actual| to the total
  // amount of bytes copied.
  void Read(uint8_t* target, size_t length, size_t* actual);

  size_t size() const { return info_.size; }

  size_t capacity() const { return capacity_; }

  size_t offset() const { return info_.offset; }

  BufferInfo* info() { return &info_; }

  uint8_t* get() { return data_.get(); }

 private:
  // Data buffer
  std::unique_ptr<uint8_t[]> data_;

  // Maximum size allocated for buffer
  size_t capacity_;

  BufferInfo info_;
};

}  // namespace internal

class ReaderInterface {
 public:
  virtual zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) = 0;
  virtual ~ReaderInterface() = default;
};

class SparseReader {
 public:
  static zx_status_t Create(fbl::unique_fd fd, std::unique_ptr<SparseReader>* out);
  static zx_status_t CreateSilent(fbl::unique_fd fd, std::unique_ptr<SparseReader>* out);

  static zx_status_t Create(std::unique_ptr<ReaderInterface> reader,
                            std::unique_ptr<SparseReader>* out);

  ~SparseReader();

  fvm::SparseImage* Image();
  fvm::PartitionDescriptor* Partitions();

  // Read requested data from sparse file into buffer
  zx_status_t ReadData(uint8_t* data, size_t length, size_t* actual);
  // Write decompressed data into new file
  zx_status_t WriteDecompressed(fbl::unique_fd outfd);

 private:
  static zx_status_t CreateHelper(std::unique_ptr<ReaderInterface> reader, bool verbose,
                                  std::unique_ptr<SparseReader>* out);

  SparseReader(std::unique_ptr<ReaderInterface> reader, bool verbose);

  // Read in header data, prepare buffers and decompression context if necessary
  zx_status_t ReadMetadata();

  // Initialize buffer with a given |size|
  static zx_status_t InitializeBuffer(size_t size, internal::Buffer* out_buf);

  // Read |length| bytes of raw data from file directly into |data|. Return |actual| bytes read.
  zx_status_t ReadRaw(uint8_t* data, size_t length, size_t* actual);

  void PrintStats() const;

  // True if sparse file is compressed
  bool compressed_;

  // If true, all logs are printed.
  bool verbose_;

  std::unique_ptr<ReaderInterface> reader_;
  std::unique_ptr<uint8_t[]> metadata_;
  LZ4F_decompressionContext_t dctx_;

  // A hint of the size of the next compressed frame to be decompressed.
  // May be an overestimate, but will not be an underestimate (0 indicates no more data left to
  // decompress).
  size_t to_read_;

  // Buffer for compressed data read directly from file
  internal::Buffer in_;
  // Buffer for decompressed data
  internal::Buffer out_;

#ifdef __Fuchsia__
  // Total time spent reading/decompressing data
  zx_ticks_t total_time_ = 0;
  // Total time spent reading data from fd
  zx_ticks_t read_time_ = 0;
#endif
};

}  // namespace fvm

#endif  // FVM_SPARSE_READER_H_
