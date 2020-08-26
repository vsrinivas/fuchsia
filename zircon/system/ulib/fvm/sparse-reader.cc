// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm/sparse-reader.h"

#include <zircon/assert.h>

#include <algorithm>
#include <cinttypes>
#include <memory>
#include <utility>
#include <vector>

namespace fvm {

namespace {

class FileReader : public ReaderInterface {
 public:
  FileReader(fbl::unique_fd fd) : fd_(std::move(fd)) {}

  virtual ~FileReader() = default;

  virtual zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) final {
    auto n = read(fd_.get(), buf, buf_size);
    if (n < 0) {
      return ZX_ERR_IO;
    }
    *size_actual = static_cast<size_t>(n);
    return ZX_OK;
  }

 private:
  fbl::unique_fd fd_;
};

}  // namespace

using Buffer = internal::Buffer;

Buffer::Buffer() = default;
Buffer::Buffer(uint64_t offset, size_t size, uint64_t capacity) : capacity_(capacity) {
  info_.size = size;
  info_.offset = offset;
  data_.reset(new uint8_t[capacity]);
}
Buffer::Buffer(Buffer&&) = default;
Buffer& Buffer::operator=(Buffer&&) = default;
Buffer::~Buffer() = default;

bool Buffer::IsEmpty() const { return info_.offset == 0 && info_.size == 0; }

void Buffer::Write(uint8_t* data, size_t length) {
  ZX_ASSERT(length <= capacity_);
  // We should have read all previous data from buffer before writing more.
  ZX_ASSERT(IsEmpty());

  if (length > 0) {
    memcpy(data_.get(), data, length);
    info_.size = length;
  }
}

void Buffer::Read(uint8_t* target, size_t length, size_t* actual) {
  size_t cp_sz = std::min(length, info_.size);

  if (cp_sz > 0) {
    memcpy(target, data_.get() + info_.offset, cp_sz);
    info_.offset += cp_sz;
  }

  info_.size -= cp_sz;

  if (info_.size == 0) {
    info_.offset = 0;
  }

  *actual = cp_sz;
}

zx_status_t SparseReader::Create(fbl::unique_fd fd, std::unique_ptr<SparseReader>* out) {
  return SparseReader::CreateHelper(std::make_unique<FileReader>(std::move(fd)), true /* verbose */,
                                    out);
}
zx_status_t SparseReader::Create(std::unique_ptr<ReaderInterface> reader,
                                 std::unique_ptr<SparseReader>* out) {
  return SparseReader::CreateHelper(std::move(reader), true /* verbose */, out);
}
zx_status_t SparseReader::CreateSilent(fbl::unique_fd fd, std::unique_ptr<SparseReader>* out) {
  return SparseReader::CreateHelper(std::make_unique<FileReader>(std::move(fd)),
                                    false /* verbose */, out);
}

zx_status_t SparseReader::CreateHelper(std::unique_ptr<ReaderInterface> reader_intf, bool verbose,
                                       std::unique_ptr<SparseReader>* out) {
  std::unique_ptr<SparseReader> reader(new SparseReader(std::move(reader_intf), verbose));

  zx_status_t status;
  if ((status = reader->ReadMetadata()) != ZX_OK) {
    return status;
  }

  *out = std::move(reader);
  return ZX_OK;
}

SparseReader::SparseReader(std::unique_ptr<ReaderInterface> reader, bool verbose)
    : compressed_(false), verbose_(verbose), reader_(std::move(reader)) {}

zx_status_t SparseReader::ReadMetadata() {
  // Read sparse image header.
  fvm::SparseImage image;
  size_t actual;
  auto status = reader_->Read(&image, sizeof(fvm::SparseImage), &actual);
  if (status != ZX_OK || actual != sizeof(fvm::SparseImage)) {
    fprintf(stderr, "failed to read the sparse header\n");
    return ZX_ERR_IO;
  }

  // Verify the header.
  if (image.magic != fvm::kSparseFormatMagic) {
    fprintf(stderr, "SparseReader: Bad magic\n");
    return ZX_ERR_BAD_STATE;
  } else if (image.version != fvm::kSparseFormatVersion) {
    fprintf(stderr, "SparseReader: Unexpected sparse file version\n");
    return ZX_ERR_BAD_STATE;
  }

  ZX_ASSERT(image.header_length > sizeof(image));
  metadata_.reset(new uint8_t[image.header_length]);
  memcpy(metadata_.get(), &image, sizeof(image));

  // Read remainder of metadata.
  size_t off = sizeof(image);
  while (off < image.header_length) {
    status = reader_->Read(&metadata_[off], image.header_length - off, &actual);
    if (status != ZX_OK) {
      fprintf(stderr, "SparseReader: Failed to read metadata\n");
      return status;
    }
    off += actual;
  }

  // If image is compressed, additional setup is required
  if (image.flags & fvm::kSparseFlagLz4) {
    if (verbose_) {
      printf("Found compressed file\n");
    }

    compressed_ = true;
    if (auto status = SetupLZ4(); status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t SparseReader::SetupLZ4() {
  zx_status_t status;
  size_t actual;
  // Initialize decompression context
  LZ4F_errorCode_t errc = LZ4F_createDecompressionContext(&dctx_, LZ4F_VERSION);
  if (LZ4F_isError(errc)) {
    fprintf(stderr, "SparseReader: could not initialize decompression: %s\n",
            LZ4F_getErrorName(errc));
    return ZX_ERR_INTERNAL;
  }

  size_t src_sz = 4;
  size_t dst_sz = 0;
  std::unique_ptr<uint8_t[]> inbufptr(new uint8_t[src_sz]);

  uint8_t* inbuf = inbufptr.get();

  // Read first 4 bytes to let LZ4 tell us how much it expects in the first pass.
  status = reader_->Read(inbuf, src_sz, &actual);
  if (status != ZX_OK || actual < src_sz) {
    fprintf(stderr, "SparseReader: could not read from input\n");
    return ZX_ERR_IO;
  }

  // Run decompress once to find out how much data we should read for the next decompress run
  // Since we are not yet decompressing any actual data, the dst_buffer is null
  to_read_ = LZ4F_decompress(dctx_, nullptr, &dst_sz, inbuf, &src_sz, NULL);
  if (LZ4F_isError(to_read_)) {
    fprintf(stderr, "SparseReader: could not decompress header: %s\n", LZ4F_getErrorName(to_read_));
    return ZX_ERR_INTERNAL;
  }

  if (to_read_ > LZ4_MAX_BLOCK_SIZE) {
    to_read_ = LZ4_MAX_BLOCK_SIZE;
  }

  // Initialize data buffers
  if ((status = InitializeBuffer(LZ4_MAX_BLOCK_SIZE, &out_)) != ZX_OK) {
    return status;
  } else if ((status = InitializeBuffer(LZ4_MAX_BLOCK_SIZE, &in_)) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t SparseReader::InitializeBuffer(size_t size, Buffer* out_buffer) {
  if (size < LZ4_MAX_BLOCK_SIZE) {
    fprintf(stderr, "Buffer size must be >= %d\n", LZ4_MAX_BLOCK_SIZE);
    return ZX_ERR_INVALID_ARGS;
  }
  *out_buffer = Buffer(0, 0, size);
  return ZX_OK;
}

SparseReader::~SparseReader() {
  PrintStats();

  if (compressed_) {
    LZ4F_freeDecompressionContext(dctx_);
  }
}

fvm::SparseImage* SparseReader::Image() {
  return reinterpret_cast<fvm::SparseImage*>(metadata_.get());
}

fvm::PartitionDescriptor* SparseReader::Partitions() {
  return reinterpret_cast<fvm::PartitionDescriptor*>(reinterpret_cast<uintptr_t>(metadata_.get()) +
                                                     sizeof(fvm::SparseImage));
}

zx_status_t SparseReader::ReadData(uint8_t* data, size_t length, size_t* actual) {
#ifdef __Fuchsia__
  zx_ticks_t start = zx_ticks_get();
#endif
  size_t total_size = 0;
  if (compressed_) {
    if (out_.IsEmpty() && to_read_ == 0) {
      // There is no more to read
      return ZX_ERR_OUT_OF_RANGE;
    }

    // Read previously decompressed data from buffer if possible
    out_.Read(data, length, &total_size);

    // If we still have data to read, start decompression (reading more from fd as needed)
    while (total_size < length && to_read_ > 0) {
      // Make sure data to read does not exceed max, and both buffers are empty
      ZX_ASSERT(out_.IsEmpty());
      ZX_ASSERT(in_.IsEmpty());
      ZX_ASSERT(to_read_ <= in_.capacity());

      // Read specified amount from fd
      zx_status_t status;
      if ((status = ReadRaw(in_.get(), to_read_, &(in_.info()->size))) != ZX_OK) {
        return status;
      }

      size_t src_sz = in_.size();
      size_t next = 0;

      // Decompress all compressed data
      while (in_.offset() < to_read_) {
        size_t dst_sz = out_.capacity() - out_.size();
        next = LZ4F_decompress(dctx_, out_.get() + out_.size(), &dst_sz, in_.get() + in_.offset(),
                               &src_sz, NULL);
        if (LZ4F_isError(next)) {
          fprintf(stderr, "could not decompress input: %s\n", LZ4F_getErrorName(next));
          return -1;
        }

        out_.info()->size += dst_sz;
        in_.info()->offset += src_sz;
        in_.info()->size -= src_sz;
        src_sz = to_read_ - in_.offset();
      }

      // Make sure we have read all data from in_buf_
      if (in_.size() > 0) {
        return ZX_ERR_IO;
      }

      in_.info()->offset = 0;

      // Copy newly decompressed data from outbuf
      size_t cp = std::min(length - total_size, static_cast<size_t>(out_.size()));
      out_.Read(data + total_size, cp, &cp);
      total_size += cp;
      to_read_ = next;

      if (to_read_ > LZ4_MAX_BLOCK_SIZE) {
        to_read_ = LZ4_MAX_BLOCK_SIZE;
      }
    }
  } else {
    zx_status_t status = ReadRaw(data, length, &total_size);

    if (status != ZX_OK) {
      return status;
    }
  }

#ifdef __Fuchsia__
  total_time_ += zx_ticks_get() - start;
#endif
  *actual = total_size;
  return ZX_OK;
}

zx_status_t SparseReader::ReadRaw(uint8_t* data, size_t length, size_t* actual) {
#ifdef __Fuchsia__
  zx_ticks_t start = zx_ticks_get();
#endif
  zx_status_t status;
  size_t size_actual;
  size_t total_size = 0;
  size_t bytes_left = length;
  while ((status = reader_->Read(data + total_size, bytes_left, &size_actual)) == ZX_OK &&
         size_actual > 0) {
    total_size += size_actual;
    bytes_left -= size_actual;
    if (bytes_left == 0) {
      break;
    }
  }

#ifdef __Fuchsia__
  read_time_ += zx_ticks_get() - start;
#endif

  if (status != ZX_OK) {
    return status;
  }

  *actual = total_size;
  return ZX_OK;
}

zx_status_t SparseReader::WriteDecompressed(fbl::unique_fd outfd) {
  if (!compressed_) {
    fprintf(stderr, "BlockReader: File is not compressed\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // Update metadata and write to new file.
  fvm::SparseImage* image = Image();
  image->flags &= ~fvm::kSparseFlagLz4;

  if (write(outfd.get(), metadata_.get(), image->header_length) !=
      static_cast<ssize_t>(image->header_length)) {
    fprintf(stderr, "BlockReader: could not write header to out file\n");
    return -1;
  }

  // Read/write decompressed data in LZ4_MAX_BLOCK_SIZE chunks.
  while (true) {
    zx_status_t status;
    uint8_t data[LZ4_MAX_BLOCK_SIZE];
    size_t length;
    if ((status = ReadData(data, LZ4_MAX_BLOCK_SIZE, &length)) != ZX_OK) {
      if (status == ZX_ERR_OUT_OF_RANGE) {
        return ZX_OK;
      }

      return status;
    }

    if (write(outfd.get(), data, length) != static_cast<ssize_t>(length)) {
      fprintf(stderr, "BlockReader: failed to write to output\n");
      return ZX_ERR_IO;
    }
  }
}

void SparseReader::PrintStats() const {
  if (verbose_) {
    printf("Reading FVM from compressed file: %s\n", compressed_ ? "true" : "false");
    printf("Remaining bytes read into compression buffer:    \%" PRIuMAX "\n", in_.size());
    printf("Remaining bytes written to decompression buffer:\%" PRIuMAX "\n", out_.size());
#ifdef __Fuchsia__
    printf("Time reading bytes from sparse FVM file:   %lu (%lu s)\n", read_time_,
           read_time_ / zx_ticks_per_second());
    printf("Time reading bytes AND decompressing them: %lu (%lu s)\n", total_time_,
           total_time_ / zx_ticks_per_second());
#endif
  }
}

zx_status_t SparseReader::DecompressLZ4File(const char* in_file, const char* out_file) {
  fbl::unique_fd fd_in(open(in_file, O_RDONLY, 0644));
  if (!fd_in) {
    fprintf(stderr, "Unable to open input lz4 file %s\n", in_file);
    return ZX_ERR_IO;
  }

  fbl::unique_fd fd_out(open(out_file, O_RDWR | O_CREAT, 0644));
  if (!fd_out) {
    fprintf(stderr, "Unable to create output file %s\n", out_file);
    return ZX_ERR_IO;
  }

  std::unique_ptr<SparseReader> reader(
      new SparseReader(std::make_unique<FileReader>(std::move(fd_in)), false));
  reader->compressed_ = true;
  reader->SetupLZ4();
  // Read/write decompressed data in LZ4_MAX_BLOCK_SIZE chunks.
  std::vector<uint8_t> buffer(LZ4_MAX_BLOCK_SIZE);
  while (true) {
    zx_status_t status;
    size_t length;
    if ((status = reader->ReadData(buffer.data(), LZ4_MAX_BLOCK_SIZE, &length)) != ZX_OK) {
      if (status == ZX_ERR_OUT_OF_RANGE) {
        return ZX_OK;
      }
      return status;
    }

    if (write(fd_out.get(), buffer.data(), length) != static_cast<ssize_t>(length)) {
      fprintf(stderr, "BlockReader: failed to write to output\n");
      return ZX_ERR_IO;
    }
  }
  return ZX_OK;
}

}  // namespace fvm
