// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <cstdio>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>

#include "src/lib/chunked-compression/chunked-compressor.h"
#include "src/lib/chunked-compression/status.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/storage/blobfs/compression/configs/chunked_compression_params.h"
#include "src/storage/tools/blobfs-compression/blobfs-compression.h"

namespace {

using ::chunked_compression::ChunkedCompressor;
using ::chunked_compression::CompressionParams;

void usage(const char* fname) {
  fprintf(stderr, "Usage: %s [--option=value ...]\n\n", fname);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  %-20s%s\n", "source_file", "(required) the file to be compressed.");
  fprintf(stderr, "  %-20s%s\n", "compressed_file", "(optional) the compressed file output path.");
  fprintf(stderr, "  %-20s%s\n", "help", "print this usage message.");
  fprintf(stderr, "  %-20s%s\n", "verbose", "show debugging information.");
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

int main(int argc, char** argv) {
  const auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    return 1;
  }
  if (cl.HasOption("verbose", nullptr)) {
    printf("Received flags:\n");
    for (const auto option : cl.options()) {
      printf("  %s = \"%s\"\n", option.name.c_str(), option.value.c_str());
    }
    printf("\n");
  }
  if (cl.HasOption("help", nullptr)) {
    usage(argv[0]);
    return 0;
  }

  // Parse command line options.
  std::string source_file, compressed_file;
  cl.GetOptionValue("source_file", &source_file);
  cl.GetOptionValue("compressed_file", &compressed_file);
  if (source_file.empty()) {
    usage(argv[0]);
    return 1;
  }

  fbl::unique_fd src_fd;
  const uint8_t* src_data;
  size_t src_size;
  if (OpenAndMapForReading(source_file.c_str(), &src_fd, &src_data, &src_size)) {
    return 1;
  }

  CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(src_size);

  uint8_t* dest_write_buf = nullptr;
  fbl::unique_fd dst_fd;
  if (!compressed_file.empty() &&
      OpenAndMapForWriting(compressed_file.c_str(), params.ComputeOutputSizeLimit(src_size),
                           &dest_write_buf, &dst_fd)) {
    return 1;
  }

  size_t compressed_size;
  if (blobfs_compress::BlobfsCompress(src_data, src_size, dest_write_buf, &compressed_size,
                                      params)) {
    return 1;
  }

  if (!compressed_file.empty()) {
    ftruncate(dst_fd.get(), compressed_size);
  }
  return 0;
}
