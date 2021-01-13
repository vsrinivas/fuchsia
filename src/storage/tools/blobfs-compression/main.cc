// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>

#include "src/lib/chunked-compression/chunked-compressor.h"
#include "src/lib/chunked-compression/status.h"
#include "src/storage/tools/blobfs-compression/blobfs-compression.h"

namespace {

using chunked_compression::ChunkedCompressor;
using chunked_compression::CompressionParams;

void usage(const char* fname) {
  fprintf(stderr, "Usage: %s source_file destination_file\n", fname);
}

// Opens |file|, truncates to |write_size|, and mmaps the file for writing.
// Returns the mapped buffer in |out_write_buf|, and the managed FD in |out_fd|.
int OpenAndMapForWriting(const char* file, size_t write_size, uint8_t** out_write_buf,
                         fbl::unique_fd* out_fd) {
  fbl::unique_fd fd(open(file, O_RDWR | O_CREAT | O_TRUNC, 0644));
  if (!fd.is_valid()) {
    fprintf(stderr, "Failed to open '%s': %s\n", file, strerror(errno));
    return 1;
  }
  if (ftruncate(fd.get(), write_size)) {
    fprintf(stderr, "Failed to truncate '%s': %s\n", file, strerror(errno));
    return 1;
  }

  void* data = nullptr;
  if (write_size > 0) {
    data = mmap(NULL, write_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      return 1;
    }
  }

  *out_write_buf = static_cast<uint8_t*>(data);
  *out_fd = std::move(fd);

  return 0;
}

// Opens |file| and mmaps the file for reading.
// Returns the mapped buffer in |out_buf|, the size of the file in |out_size|, and the managed FD in
// |out_fd|.
int OpenAndMapForReading(const char* file, fbl::unique_fd* out_fd, const uint8_t** out_buf,
                         size_t* out_size) {
  fbl::unique_fd fd(open(file, O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "Failed to open '%s'.\n", file);
    return 1;
  }
  size_t size;
  struct stat info;
  if (fstat(fd.get(), &info) < 0) {
    fprintf(stderr, "stat(%s) failed: %s\n", file, strerror(errno));
    return 1;
  }
  if (!S_ISREG(info.st_mode)) {
    fprintf(stderr, "%s is not a regular file\n", file);
    return 1;
  }
  size = info.st_size;

  void* data = nullptr;
  if (size > 0) {
    data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      return 1;
    }
  }

  *out_fd = std::move(fd);
  *out_buf = static_cast<uint8_t*>(data);
  *out_size = size;

  return 0;
}
}  // namespace

int main(int argc, char* const* argv) {
  if (argc < 3) {
    usage(argv[0]);
    return 1;
  }

  fbl::unique_fd src_fd;
  const uint8_t* src_data;
  size_t src_size;
  if (OpenAndMapForReading(argv[1] /*src file*/, &src_fd, &src_data, &src_size)) {
    return 1;
  }

  CompressionParams params = blobfs_compress::ComputeDefaultBlobfsCompressionParams(src_size);

  fbl::unique_fd dst_fd;
  uint8_t* dest_write_buf;
  if (OpenAndMapForWriting(argv[2] /*destination file*/, params.ComputeOutputSizeLimit(src_size),
                           &dest_write_buf, &dst_fd)) {
    return 1;
  }

  size_t compressed_size;
  if (blobfs_compress::BlobfsCompress(src_data, src_size, dest_write_buf /*destination buffer*/,
                                      &compressed_size, params)) {
    return 1;
  }
  ftruncate(dst_fd.get(), compressed_size);
  return 0;
}
